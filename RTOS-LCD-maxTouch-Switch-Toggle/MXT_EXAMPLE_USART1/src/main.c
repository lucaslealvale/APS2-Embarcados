/*------------------------------------------------------------------------/
/*                  APS-2 DE COMPUTAÇÃO EMBARCADA                                                            

Authors:
- Pedro Vero Fontes
- Lucas Leal Vale

Professor:
- Rafael Corsi

TO FIX:
- vMedia n funciona
- lixo no print led

/
/-------------------------------------------------------------------------*/

#include <asf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "conf_board.h"
#include "conf_uart_serial.h"
#include "maxTouch/maxTouch.h"

/************************************************************************/
/* FONTES                                                               */
/************************************************************************/
#include "fonts/tfont.h"
#include "fonts/digital521.h"
#include "fonts/arial_72.h"
#include "fonts/sourcecodepro_28.h"
#include "fonts/calibri_36.h"

/************************************************************************/
/* IMAGENS                                                              */
/************************************************************************/
#include "icones/bike.h"
#include "icones/cronon.h"
#include "icones/down_arrow.h"
#include "icones/pause.h"
#include "icones/play.h"
#include "icones/reset.h"
#include "icones/up_arrow.h"

/************************************************************************/
/* prototypes                                                           */
/************************************************************************/

void callback(void);
static void RTT_init(uint16_t pllPreScale, uint32_t IrqNPulses);

/************************************************************************/
/* LCD + TOUCH                                                          */
/************************************************************************/

#define MAX_ENTRIES        10

struct ili9488_opt_t g_ili9488_display_opt;
const uint32_t BUTTON_W = 120;
const uint32_t BUTTON_H = 150;
const uint32_t BUTTON_BORDER = 2;
const uint32_t BUTTON_X = ILI9488_LCD_WIDTH/2;
const uint32_t BUTTON_Y = ILI9488_LCD_HEIGHT/2;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_MXT_STACK_SIZE            (2*1024/sizeof(portSTACK_TYPE))
#define TASK_MXT_STACK_PRIORITY        (tskIDLE_PRIORITY)

#define TASK_LCD_STACK_SIZE            (4*1024/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY        (tskIDLE_PRIORITY)

/************************************************************************/
/* LED + BOTÃO                                                          */
/************************************************************************/

// LED
#define LED_PIO       PIOC
#define LED_PIO_ID    ID_PIOC
#define LED_IDX       8
#define LED_IDX_MASK  (1 << LED_IDX)

// Botão
#define BUT_PIO       PIOA
#define BUT_PIO_ID    ID_PIOA
#define BUT_IDX       11
#define BUT_IDX_MASK  (1 << BUT_IDX)

/************************************************************************/
/* OBJETOS                                                              */
/************************************************************************/

typedef struct {
  uint x;
  uint y;
} touchData;

typedef struct {
	uint32_t width;     // largura (px)
	uint32_t height;    // altura  (px)
	uint32_t x;         // posicao x
	uint32_t y;         // posicao y
  uint8_t *data;
	uint8_t status;
	void (*func)(void);
	
} t_but;

typedef struct {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t seccond;
} calendar;

/************************************************************************/
/* FLAGS                                                                */
/************************************************************************/

volatile char flag_rtc = 0;
volatile Bool f_rtt_alarme = false;
char flag_led = 0;
char flag_playpause = 0;
char flag_seta = 0;
char flag_rtt = 0;

/************************************************************************/
/* CRONOMETRO                                                           */
/************************************************************************/

int h = 0;
int m = 0;
int s = 0;

/************************************************************************/
/* RODADAS                                                              */
/************************************************************************/

int N = 0;
double pi = 3.141492;
double dT= 1.0;
double r = 0.311;
double nTotal = 0;

double interacoes = 0.0;

double dTotal = 0;
double vMedia = 0;
double v = 0;


/************************************************************************/
/* FILAS                                                                */
/************************************************************************/

QueueHandle_t xQueueTouch;
QueueHandle_t xQueuePulsos;

/************************************************************************/
/* SEMAFOROS                                                            */
/************************************************************************/

SemaphoreHandle_t xSemaphore = NULL;
SemaphoreHandle_t xSemaphore_playpause = NULL;
SemaphoreHandle_t xSemaphore_reset = NULL;
SemaphoreHandle_t xSemaphore_rodadas = NULL;

/************************************************************************/
/* handler/callbacks                                                    */
/************************************************************************/

void callback(void){
  printf("CLICK!\n");
  xSemaphoreGive(xSemaphore);
}

void callback_playpause(void){
  printf("CLICK playpause!\n");
  xSemaphoreGive(xSemaphore_playpause);
}

void callback_seta(void){
  printf("CLICK seta!\n");
}

void callback_reset(void){
  printf("CLICK reset!\n");
  xSemaphoreGive(xSemaphore_reset);
}

void callback_rodadas(void){
  N++;
}


/************************************************************************/
/* RTOS hooks                                                           */
/************************************************************************/

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,
signed char *pcTaskName)
{
  printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
  for (;;) {
  }
}
extern void vApplicationIdleHook(void)
{
  pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
}
extern void vApplicationTickHook(void)
{
}
extern void vApplicationMallocFailedHook(void)
{
  configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* init                                                                 */
/************************************************************************/

void RTC_Handler(void)
{
	uint32_t ul_status = rtc_get_status(RTC);
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC)
	{
		rtc_clear_status(RTC, RTC_SCCR_SECCLR);
		flag_rtc = 1;
	}

	rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
	rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
	rtc_clear_status(RTC, RTC_SCCR_CALCLR);
	rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type)
{
	pmc_enable_periph_clk(ID_RTC);
	rtc_set_hour_mode(rtc, 0);
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.seccond);
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);
	rtc_enable_interrupt(rtc, irq_type);
}

void RTT_Handler(void)
{
  uint32_t ul_status;
  
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(xQueuePulsos, &N,  &xHigherPriorityTaskWoken);
  N = 0;

  /* Get RTT status - ACK */
  ul_status = rtt_get_status(RTT);
}

static void RTT_init(uint16_t pllPreScale, uint32_t IrqNPulses)
{
  uint32_t ul_previous_time;

  /* Configure RTT for a 1 second tick interrupt */
  rtt_sel_source(RTT, false);
  rtt_init(RTT, pllPreScale);
  
  ul_previous_time = rtt_read_timer_value(RTT);
  while (ul_previous_time == rtt_read_timer_value(RTT));
  
  rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);

  /* Enable RTT interrupt */
  NVIC_DisableIRQ(RTT_IRQn);
  NVIC_ClearPendingIRQ(RTT_IRQn);
  NVIC_SetPriority(RTT_IRQn, 4);
  NVIC_EnableIRQ(RTT_IRQn);
  rtt_enable_interrupt(RTT, RTT_MR_ALMIEN);
}

static void configure_lcd(void){
  g_ili9488_display_opt.ul_width = ILI9488_LCD_WIDTH;
  g_ili9488_display_opt.ul_height = ILI9488_LCD_HEIGHT;
  g_ili9488_display_opt.foreground_color = COLOR_CONVERT(COLOR_WHITE);
  g_ili9488_display_opt.background_color = COLOR_CONVERT(COLOR_WHITE);
  ili9488_init(&g_ili9488_display_opt);
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

int process_touch(t_but botoes[], touchData touch, uint32_t n){

  for (int i = 0; i < n; i++){

    int x_limit_up   = botoes[i].x;
    int x_limit_down = botoes[i].x + botoes[i].width;

    int y_limit_up   = botoes[i].y;
    int y_limit_down = botoes[i].y + botoes[i].height;
  
    if (touch.x > x_limit_up && touch.x < x_limit_down && touch.y > y_limit_up && touch.y < y_limit_down){
      return i;
    }

  }
  
  return -1;
}

void draw_screen(void) {
  ili9488_set_foreground_color(COLOR_CONVERT(COLOR_WHITE));
  ili9488_draw_filled_rectangle(0, 0, ILI9488_LCD_WIDTH-1, ILI9488_LCD_HEIGHT-1);
}

void draw_button(uint32_t clicked) {
  static uint32_t last_state = 255;
  if(clicked == last_state) return;
  
  ili9488_set_foreground_color(COLOR_CONVERT(COLOR_BLACK));
  ili9488_draw_filled_rectangle(BUTTON_X-BUTTON_W/2, BUTTON_Y-BUTTON_H/2, BUTTON_X+BUTTON_W/2, BUTTON_Y+BUTTON_H/2);
  if(clicked) {
    ili9488_set_foreground_color(COLOR_CONVERT(COLOR_TOMATO));
    ili9488_draw_filled_rectangle(BUTTON_X-BUTTON_W/2+BUTTON_BORDER, BUTTON_Y+BUTTON_BORDER, BUTTON_X+BUTTON_W/2-BUTTON_BORDER, BUTTON_Y+BUTTON_H/2-BUTTON_BORDER);
    } else {
    ili9488_set_foreground_color(COLOR_CONVERT(COLOR_GREEN));
    ili9488_draw_filled_rectangle(BUTTON_X-BUTTON_W/2+BUTTON_BORDER, BUTTON_Y-BUTTON_H/2+BUTTON_BORDER, BUTTON_X+BUTTON_W/2-BUTTON_BORDER, BUTTON_Y-BUTTON_BORDER);
  }
  last_state = clicked;
}

uint32_t convert_axis_system_x(uint32_t touch_y) {
  
  return ILI9488_LCD_WIDTH - ILI9488_LCD_WIDTH*touch_y/4096;
}

uint32_t convert_axis_system_y(uint32_t touch_x) {
  
  return ILI9488_LCD_HEIGHT*touch_x/4096;
}

void update_screen(uint32_t tx, uint32_t ty) {
  if(tx >= BUTTON_X-BUTTON_W/2 && tx <= BUTTON_X + BUTTON_W/2) {
    if(ty >= BUTTON_Y-BUTTON_H/2 && ty <= BUTTON_Y) {
      draw_button(1);
      } else if(ty > BUTTON_Y && ty < BUTTON_Y + BUTTON_H/2) {
      draw_button(0);
    }
  }
}

void font_draw_text(tFont *font, const char *text, int x, int y, int spacing) {
  char *p = text;
  while(*p != NULL) {
    char letter = *p;
    int letter_offset = letter - font->start_char;
    if(letter <= font->end_char) {
      tChar *current_char = font->chars + letter_offset;
      ili9488_draw_pixmap(x, y, current_char->image->width, current_char->image->height, current_char->image->data);
      x += current_char->image->width + spacing;
    }
    p++;
  }
}

void mxt_handler(struct mxt_device *device, uint *x, uint *y)
{
  uint8_t i = 0; 

  struct mxt_touch_event touch_event;
  
  uint first = 0;

  do {

    if (mxt_read_touch_event(device, &touch_event) != STATUS_OK) {
      continue;
    }
    
    // Envia dados via fila RTOS
    if(first == 0 ){
      *x = convert_axis_system_x(touch_event.y);
      *y = convert_axis_system_y(touch_event.x);
      first = 1;
    }
    
    i++;

  } while ((mxt_is_message_pending(device)) & (i < MAX_ENTRIES));
}

void init(void){
	// Initialize the board clock
	sysclk_init();

	// Disativa WatchDog Timer
	WDT->WDT_MR = WDT_MR_WDDIS;

	pmc_enable_periph_clk(LED_PIO_ID);
	pio_configure(LED_PIO, PIO_OUTPUT_0, LED_IDX_MASK, PIO_DEFAULT);

  pmc_enable_periph_clk(BUT_PIO_ID);
  pio_configure(BUT_PIO, PIO_INPUT, BUT_IDX_MASK, PIO_PULLUP | PIO_DEBOUNCE);
  pio_set_debounce_filter(BUT_PIO, BUT_IDX_MASK, 100);
  pio_handler_set(BUT_PIO, BUT_PIO_ID, BUT_IDX_MASK, PIO_IT_RISE_EDGE, callback_rodadas);
  pio_enable_interrupt(BUT_PIO, BUT_IDX_MASK);
  delay_ms(50);
  pio_get_interrupt_status(BUT_PIO);
  NVIC_SetPriority(BUT_PIO_ID, 4);
  NVIC_EnableIRQ(BUT_PIO_ID);

}

void show_cronon(int h, int m, int s){

  char c[200];

  sprintf(c, "%02d:%02d:%02d", h, m, s);
  font_draw_text(&calibri_36, c, 100, ILI9488_LCD_HEIGHT/2 + 45, 1);

}

void show_clock(calendar rtc_initial){
  if (flag_rtc){

    rtc_get_time(RTC, &rtc_initial.hour, &rtc_initial.minute, &rtc_initial.seccond);

    char b[200];
    sprintf(b, "%02d:%02d:%02d", rtc_initial.hour, rtc_initial.minute, rtc_initial.seccond);
    font_draw_text(&calibri_36, b, 85, 0, 1);
    
    flag_rtc = 0;

    if (flag_playpause){
      if(s < 59) s++;
      else{
        s = 0;
        if (m < 59) m++;
        else {
          h++;
          m = 0;
        }
      }
    }

  }
}

/************************************************************************/
/* tasks                                                                */
/************************************************************************/

void task_mxt(void){
  
  struct mxt_device device; 
  mxt_init(&device);
  touchData touch;
  
  while (true) {
    
    if (mxt_is_message_pending(&device)) {
      mxt_handler(&device, &touch.x, &touch.y);
      xQueueSend( xQueueTouch, &touch, 0);  
      vTaskDelay(200);
      
      // limpa touch
      while (mxt_is_message_pending(&device)){
        mxt_handler(&device, NULL, NULL);
        vTaskDelay(50);
      }
    }
    
    vTaskDelay(300);
  }
}

void task_lcd(void){
	xQueueTouch = xQueueCreate( 10, sizeof( touchData ) );
  xQueuePulsos = xQueueCreate( 10, sizeof( uint ) );

  xSemaphore = xSemaphoreCreateBinary();
  xSemaphore_playpause = xSemaphoreCreateBinary();
  xSemaphore_reset = xSemaphoreCreateBinary();
  xSemaphore_rodadas = xSemaphoreCreateBinary();

	configure_lcd();
	draw_screen();

	// font_draw_text(&calibri_36, "HORA", 0, 0, 1);

	t_but reset_but = {.width = reset.width, .height = reset.height,
	  .x = ILI9488_LCD_WIDTH-125, .y = ILI9488_LCD_HEIGHT-125, .data = reset.data, 
    .status = 1, .func = &callback_reset
  };
	
  t_but play_but = {.width = play.width, .height = play.height,
	  .x = 5, .y = ILI9488_LCD_HEIGHT-125, .data = play.data, 
    .status = 1, .func = &callback_playpause
  };

  t_but pause_but = {.width = pause.width, .height = pause.height,
	  .x = 5, .y = ILI9488_LCD_HEIGHT-125, .data = pause.data, 
    .status = 1, .func = &callback_playpause
  };

	t_but down_arrow_but = {.width = down_arrow.width, .height = down_arrow.height,
    .x = ILI9488_LCD_WIDTH - 80, .y =  ILI9488_LCD_HEIGHT/2 - 160, .data = down_arrow.data,
    .status = 1, .func = &callback_seta
  };
  
	t_but up_arrow_but = {.width = up_arrow.width, .height = up_arrow.height,
    .x = ILI9488_LCD_WIDTH - 80, .y =  ILI9488_LCD_HEIGHT/2 - 160, .data = up_arrow.data,
    .status = 1, .func = &callback_seta
  };
	
	t_but bike_but = {.width = bike.width, .height = bike.height,	
    .x = 5, .y = ILI9488_LCD_HEIGHT/2 - 60, .data = bike.data,
    .status = 1, .func = &callback
  };

	t_but cronometro_but = {.width = cronon.width, .height = cronon.height,		
    .x = 5, .y = ILI9488_LCD_HEIGHT/2 + 20, .data = cronon.data,
    .status = 1, .func = &callback
  };

	
  ili9488_set_foreground_color(COLOR_CONVERT(COLOR_BLACK));

  ili9488_draw_pixmap(bike_but.x, bike_but.y, bike.width, bike.height, bike.data);
  ili9488_draw_pixmap(cronometro_but.x, cronometro_but.y, cronon.width, cronon.height, cronon.data);

  ili9488_draw_pixmap(reset_but.x, reset_but.y, reset_but.width, reset_but.height, reset_but.data);

	
	t_but botoes[] = {reset_but, play_but, pause_but, down_arrow_but, up_arrow_but, bike_but, cronometro_but};

	// struct local para armazenar msg enviada pela task do mxt
	touchData touch;

  // RTC RELOGIO + CRONOMETRO 
  calendar rtc_initial = {2018, 3, 19, 12, 15, 0, 1};
  RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);

  // força inicializacao do rtt
  // colocando dado fake na fila
  uint n = 0;
  xQueueSend(xQueuePulsos, &n , 0);

	while (true) {
		if (xQueueReceive( xQueueTouch, &(touch), ( TickType_t )  100 / portTICK_PERIOD_MS)) { //500 ms
			int b = process_touch(botoes, touch, 7);
			if(b >= 0){

        botoes[b].func();
				botoes[b].status = !botoes[b].status;
			
      }

			printf("x:%d y:%d\n", touch.x, touch.y);
			printf("b:%d\n", b);
		}

    // RELOGIO + CRONOMETRO
    show_clock(rtc_initial);
    show_cronon(h, m, s);
    
  
    // calcular a velocidade com o rtt aqui: todas as vezes que apertar o botao salvar->calcular->printar
    

    if (flag_playpause){
      ili9488_draw_pixmap(pause_but.x, pause_but.y, pause_but.width, pause_but.height, pause_but.data); 

	    ili9488_set_foreground_color(COLOR_CONVERT(COLOR_TOMATO));
	    ili9488_draw_filled_circle(cronometro_but.x + 245, cronometro_but.y + 40, 8);
      f_rtt_alarme = true;
    }
    else{
      ili9488_draw_pixmap(play_but.x,play_but.y, play.width, play.height, play.data);

      ili9488_set_foreground_color(COLOR_CONVERT(COLOR_WHITE));
	    ili9488_draw_filled_circle(cronometro_but.x + 245, cronometro_but.y + 40, 10);
    }

    if (flag_seta){
	    ili9488_draw_pixmap(down_arrow_but.x, down_arrow_but.y, down_arrow.width, down_arrow.height, down_arrow.data);
    }
    else{
      ili9488_draw_pixmap(up_arrow_but.x, up_arrow_but.y, up_arrow.width, up_arrow.height, up_arrow.data);
    }

    if( xSemaphoreTake(xSemaphore, 0) == pdTRUE ){   
      flag_led = !flag_led;
    }
    
    if( xSemaphoreTake(xSemaphore_playpause, 0) == pdTRUE ){   
      flag_playpause = !flag_playpause;
    }

    if( xSemaphoreTake(xSemaphore_reset, 0) == pdTRUE ){   
      
      flag_playpause = 0;

      h = 0;
      m = 0;
      s = 0;

      dTotal = 0;
      vMedia = 0;
      nTotal = 0;
      interacoes = 0;
      
    }
    double v_old = 0;
    if( xQueueReceive(xQueuePulsos, &n, 0) == pdTRUE ){ 
      //-------------------------------------------------
      // inicializa novo RTT  
      //  IRQ apos 4s -> 8*0.5
      uint16_t pllPreScale = (int) (((float) 32768) / 4.0);
      uint32_t irqRTTvalue = 4*dT;
      
      // reinicia RTT para gerar um novo IRQ
      RTT_init(pllPreScale, irqRTTvalue);         
      //-------------------------------------------------

      printf("%d\n", n);

      interacoes += 1.0;

      // velocidade angular
      double w = 2*pi*n/dT;
      
      //velocidade instantanea 
      v_old = v;
      v = w*r;

      // aceleracao
      double a = (v - v_old)/dT;

      if (flag_playpause){ 
        nTotal += n;
        dTotal = nTotal*2*pi*r*(0.001);
        vMedia = dTotal*3600/(dT*interacoes);
        
      }

      printf("%lf\n", dTotal);
      printf("%lf\n", v);
      printf("%lf\n", vMedia);
      printf("dT %lf\n", dT);
      printf("interacoes %lf\n", interacoes);
      

      if (a < 0) flag_seta = 1;
      if (a > 0) flag_seta = 0;      
    }

    // printf("%0k.yf" float_variable_name)

    /* 
      Here k is the total number of characters you want to get printed. 
      k = x + 1 + y (+ 1 for the dot) and float_variable_name is the float 
      variable that you want to get printed.
    */

    char velo[200];
    sprintf(velo, "%04.1f", v*3.6);
    font_draw_text(&calibri_36, velo, 70, ILI9488_LCD_HEIGHT/2 - 140, 1);
    
    char KM[200];
    sprintf(KM, "Km/h");
    font_draw_text(&calibri_36, KM,150, ILI9488_LCD_HEIGHT/2 - 140, 1);
    

    char dist[200];
    sprintf(dist, "%04.1f Km", dTotal, vMedia);
    font_draw_text(&calibri_36, dist, 100, ILI9488_LCD_HEIGHT/2 - 45, 1);

    char vM[200];
    sprintf(vM, "(%04.1f)", vMedia);
    font_draw_text(&calibri_36, vM, 230, ILI9488_LCD_HEIGHT/2 - 45, 1);

    if(flag_led){
      pio_clear(PIOC, LED_IDX_MASK);  
    }
    else{
      pio_set(PIOC, LED_IDX_MASK);                 
    }

	}
  
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/

int main(void)
{
  /* Initialize the USART configuration struct */
  const usart_serial_options_t usart_serial_options = {
    .baudrate     = USART_SERIAL_EXAMPLE_BAUDRATE,
    .charlength   = USART_SERIAL_CHAR_LENGTH,
    .paritytype   = USART_SERIAL_PARITY,
    .stopbits     = USART_SERIAL_STOP_BIT
  };

  sysclk_init(); /* Initialize system clocks */
  board_init();  /* Initialize board */
  init();
  
  /* Initialize stdio on USART */
  stdio_serial_init(USART_SERIAL_EXAMPLE, &usart_serial_options);
  
  /* Create task to handler touch */
  if (xTaskCreate(task_mxt, "mxt", TASK_MXT_STACK_SIZE, NULL, TASK_MXT_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create test led task\r\n");
  }
  
  /* Create task to handler LCD */
  if (xTaskCreate(task_lcd, "lcd", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create test led task\r\n");
  }
  
  /* Start the scheduler. */
  vTaskStartScheduler();

  while(1){

  }

  return 0;
}
