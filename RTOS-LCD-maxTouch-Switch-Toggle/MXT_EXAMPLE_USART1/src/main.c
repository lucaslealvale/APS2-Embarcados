#include <asf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "conf_board.h"
#include "conf_uart_serial.h"
#include "maxTouch/maxTouch.h"
#include "tfont.h"
#include "digital521.h"

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
/* Botoes lcd                                                           */
/************************************************************************/


/************************************************************************/
/* RTOS                                                                  */
/************************************************************************/
#define TASK_MXT_STACK_SIZE            (2*1024/sizeof(portSTACK_TYPE))
#define TASK_MXT_STACK_PRIORITY        (tskIDLE_PRIORITY)

#define TASK_LCD_STACK_SIZE            (4*1024/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY        (tskIDLE_PRIORITY)

// LED
#define LED_PIO       PIOC
#define LED_PIO_ID    ID_PIOC
#define LED_IDX       8
#define LED_IDX_MASK  (1 << LED_IDX)

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

QueueHandle_t xQueueTouch;
SemaphoreHandle_t xSemaphore = NULL;
SemaphoreHandle_t xSemaphore_playpause = NULL;
SemaphoreHandle_t xSemaphore_seta = NULL;

/************************************************************************/
/* handler/callbacks                                                    */
/************************************************************************/


void callback(void){
  printf("CLICK!\n");

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken);
}

void callback_playpause(void){
  printf("CLICK 2!\n");

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xSemaphore_playpause, &xHigherPriorityTaskWoken);
}

void callback_seta(void){
  printf("CLICK 3!\n");

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xSemaphore_seta, &xHigherPriorityTaskWoken);
}

/************************************************************************/
/* RTOS hooks                                                           */
/************************************************************************/

/**
* \brief Called if stack overflow during execution
*/
extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,
signed char *pcTaskName)
{
  printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
  /* If the parameters have been corrupted then inspect pxCurrentTCB to
  * identify which task has overflowed its stack.
  */
  for (;;) {
  }
}

/**
* \brief This function is called by FreeRTOS idle task
*/
extern void vApplicationIdleHook(void)
{
  pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
}

/**
* \brief This function is called by FreeRTOS each tick
*/
extern void vApplicationTickHook(void)
{
}

extern void vApplicationMallocFailedHook(void)
{
  /* Called if a call to pvPortMalloc() fails because there is insufficient
  free memory available in the FreeRTOS heap.  pvPortMalloc() is called
  internally by FreeRTOS API functions that create tasks, queues, software
  timers, and semaphores.  The size of the FreeRTOS heap is set by the
  configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */

  /* Force an assert. */
  configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* init                                                                 */
/************************************************************************/

static void configure_lcd(void){
  /* Initialize display parameter */
  g_ili9488_display_opt.ul_width = ILI9488_LCD_WIDTH;
  g_ili9488_display_opt.ul_height = ILI9488_LCD_HEIGHT;
  g_ili9488_display_opt.foreground_color = COLOR_CONVERT(COLOR_WHITE);
  g_ili9488_display_opt.background_color = COLOR_CONVERT(COLOR_WHITE);

  /* Initialize LCD */
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
  static uint32_t last_state = 255; // undefined
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
  // entrada: 4096 - 0 (sistema de coordenadas atual)
  // saida: 0 - 320
  return ILI9488_LCD_WIDTH - ILI9488_LCD_WIDTH*touch_y/4096;
}

uint32_t convert_axis_system_y(uint32_t touch_x) {
  // entrada: 0 - 4096 (sistema de coordenadas atual)
  // saida: 0 - 320
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
  /* USART tx buffer initialized to 0 */
  uint8_t i = 0; /* Iterator */

  /* Temporary touch event data struct */
  struct mxt_touch_event touch_event;
  
  /* first touch only */
  uint first = 0;

  /* Collect touch events and put the data in a string,
  * maximum 2 events at the time */
  do {

    /* Read next next touch event in the queue, discard if read fails */
    if (mxt_read_touch_event(device, &touch_event) != STATUS_OK) {
      continue;
    }
    
    /************************************************************************/
    /* Envia dados via fila RTOS                                            */
    /************************************************************************/
    if(first == 0 ){
      *x = convert_axis_system_x(touch_event.y);
      *y = convert_axis_system_y(touch_event.x);
      first = 1;
    }
    
    i++;

    /* Check if there is still messages in the queue and
    * if we have reached the maximum numbers of events */
  } while ((mxt_is_message_pending(device)) & (i < MAX_ENTRIES));
}

void init(void){
	// Initialize the board clock
	sysclk_init();

	// Disativa WatchDog Timer
	WDT->WDT_MR = WDT_MR_WDDIS;

	pmc_enable_periph_clk(LED_PIO_ID);
	pio_configure(LED_PIO, PIO_OUTPUT_0, LED_IDX_MASK, PIO_DEFAULT);

}

/************************************************************************/
/* tasks                                                                */
/************************************************************************/

void task_mxt(void){
  
  struct mxt_device device; /* Device data container */
  mxt_init(&device);       	/* Initialize the mXT touch device */
  touchData touch;          /* touch queue data type*/
  
  while (true) {
    /* Check for any pending messages and run message handler if any
    * message is found in the queue */
    if (mxt_is_message_pending(&device)) {
      mxt_handler(&device, &touch.x, &touch.y);
      xQueueSend( xQueueTouch, &touch, 0);           /* send mesage to queue */
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
  
  xSemaphore = xSemaphoreCreateBinary();
  xSemaphore_playpause = xSemaphoreCreateBinary();
  xSemaphore_seta = xSemaphoreCreateBinary();

	configure_lcd();
	draw_screen();
	// font_draw_text(&digital52, "DEMO - BUT", 0, 0, 1);

	t_but reset_but = {.width = reset.width, .height = reset.height,
	  .x = ILI9488_LCD_WIDTH-125, .y = ILI9488_LCD_HEIGHT-125, .data = reset.data, 
    .status = 1, .func = &callback
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

  // ILI9488_LCD_WIDTH
  // ILI9488_LCD_HEIGHT

	// desenha imagem background na posicao X=80 e Y=150

  ili9488_draw_pixmap(bike_but.x, bike_but.y, bike.width, bike.height, bike.data);
  ili9488_draw_pixmap(cronometro_but.x, cronometro_but.y, cronon.width, cronon.height, cronon.data);
	
  ili9488_draw_pixmap(reset_but.x, reset_but.y, reset_but.width, reset_but.height, reset_but.data);
	
	t_but botoes[] = {reset_but, play_but, pause_but, down_arrow_but,up_arrow_but, bike_but, cronometro_but};


	// struct local para armazenar msg enviada pela task do mxt
	touchData touch;

  char flag_led = 0;
  char flag_playpause = 0;
  char flag_seta = 0;
  
	while (true) {
		if (xQueueReceive( xQueueTouch, &(touch), ( TickType_t )  500 / portTICK_PERIOD_MS)) { //500 ms
			int b = process_touch(botoes, touch, 7);
			if(b >= 0){

        botoes[b].func();

				botoes[b].status = !botoes[b].status;
			}

			printf("x:%d y:%d\n", touch.x, touch.y);
			printf("b:%d\n", b);
		}

    if (flag_playpause){
      ili9488_draw_pixmap(pause_but.x, pause_but.y, pause_but.width, pause_but.height, pause_but.data); 
    }
    else{
      ili9488_draw_pixmap(play_but.x,play_but.y, play.width, play.height, play.data);
    }

    if (flag_seta){
	    ili9488_draw_pixmap(down_arrow_but.x, down_arrow_but.y, down_arrow.width, down_arrow.height, down_arrow.data);
    }
    else{
      ili9488_draw_pixmap(up_arrow_but.x, up_arrow_but.y, up_arrow.width, up_arrow.height, up_arrow.data);
    }

    if( xSemaphoreTake(xSemaphore, 0) == pdTRUE ){   
      flag_led = ! flag_led;
    }
    
    if( xSemaphoreTake(xSemaphore_playpause, 0) == pdTRUE ){   
      flag_playpause = ! flag_playpause;
    }

    if( xSemaphoreTake(xSemaphore_seta, 0) == pdTRUE ){   
      flag_seta = ! flag_seta;
    }

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
