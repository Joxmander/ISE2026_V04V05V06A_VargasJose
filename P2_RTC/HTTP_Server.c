/*------------------------------------------------------------------------------
 * MDK Middleware - Component ::Network
 * Copyright (c) 2004-2019 Arm Limited (or its affiliates). All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    HTTP_Server.c
 * Purpose: HTTP Server example
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include "string.h"
#include <stdlib.h>

#include "rl_net.h"                     // Keil.MDK-Pro::Network:CORE
#include "stm32f4xx_hal.h"              // Keil::Device:STM32Cube HAL:Common
#include "Board_LED.h"                  // ::Board Support:LED
#include "main.h"

// Cabeceras de mis librerias
#include "lcd.h"
#include "adc.h"
#include "rtc.h"
#include "lcd.h"

/* --- CONFIGURACIÓN DEL HILO PRINCIPAL (app_main) --- */
// Main stack size must be multiple of 8 Bytes
#define APP_MAIN_STK_SZ (1024U)
uint64_t app_main_stk[APP_MAIN_STK_SZ / 8];
const osThreadAttr_t app_main_attr = {
  .stack_mem  = &app_main_stk[0],
  .stack_size = sizeof(app_main_stk)
};

/* --- VARIABLES GLOBALES COMPARTIDAS CON LA WEB --- */
/* Estas variables son externas porque el archivo 'HTTP_Server_CGI.c' 
   las leerá o modificará cuando el usuario interactúe con el navegador. */
bool LEDrun;               // Controla si el parpadeo de LEDs está activo
char lcd_text[2][20+1];    // Almacena el texto que el usuario escribe en la web

//extern void     netDHCP_Notify (uint32_t if_num, uint8_t option, const uint8_t *val, uint32_t len);

extern osThreadId_t TID_Display;
extern osThreadId_t TID_Led;

/* Handler del ADC (Potenciómetro) */
ADC_HandleTypeDef hadc1;
													 
/* Thread IDs */
osThreadId_t TID_Display;
osThreadId_t TID_Led;

/* Thread declarations */
static void BlinkLed (void *arg);
void Time_Thread (void *argument);
													 
__NO_RETURN void app_main (void *arg);

/*----------------------------------------------------------------------------
  Funciones de soporte para el Servidor Web (CGI)
 *---------------------------------------------------------------------------*/

/**
  * @brief Lee el valor del potenciómetro. 
  * Esta función es llamada por el servidor WEB cada vez que refrescas la página ad.cgi.
  */
uint16_t AD_in (uint32_t ch) {
  ADC_ChannelConfTypeDef sConfig = {0};
  
  // Seleccionamos el canal según el diseño de la placa Nucleo (PC0 = IN10)
  sConfig.Channel = ADC_CHANNEL_10; //???????????????????? NO estoy seguro si es el 3 o el 10
  sConfig.Rank = 1;	// Rank 1 significa que este canal será el primero (y único) en ser leído.
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;	 //????????NO se si poner mas. Es el tiempo que el micro deja abierto el "grifo" para cargar el condensador interno.
  
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  return 0;
}

/**
  * @brief Devuelve el estado de los botones.
  * Requerida por el servidor web para pintar los checkboxes en la web.
  */
uint8_t get_button (void) {
  // No estamos usando el driver de botones del BSP, 
  // devolvemos 0 para que el servidor web no falle al compilar.
  return 0; 
}

/* IP address change notification */
void netDHCP_Notify (uint32_t if_num, uint8_t option, const uint8_t *val, uint32_t len) {

  (void)if_num;
  (void)val;
  (void)len;

  if (option == NET_DHCP_OPTION_IP_ADDRESS) {
    /* IP address change, trigger LCD update */
 //   osThreadFlagsSet (TID_Display, 0x01);
  }
}


/*----------------------------------------------------------------------------
  Thread 'BlinkLed': Blink the LEDs on an eval board
 *---------------------------------------------------------------------------*/
static __NO_RETURN void BlinkLed (void *arg) {
	// 1. Matriz de patrones (secuencia)
  // Cada número hexadecimal representa 8 bits (un LED por bit).
  // Por ejemplo: 0x21 es 0010 0001 en binario (LED 5 y LED 0 encendidos).
  const uint8_t led_val[16] = { 0x48,0x88,0x84,0x44,0x42,0x22,0x21,0x11,
                                0x12,0x0A,0x0C,0x14,0x18,0x28,0x30,0x50 };
  uint32_t cnt = 0U;	// Índice para recorrer la matriz

  (void)arg;

  LEDrun = true;	//Estado inicial
  while(1) {
		// 3. Comprobación del "Interruptor de Software"
    // 'LEDrun' es una variable global. 
    // Si desde la WEB el usuario pulsa "Stop", LEDrun pasa a ser false y los LEDs se quedan quietos.
    /* Every 100 ms */
    if (LEDrun == true) {
      LED_SetOut (led_val[cnt]);	// envía el byte completo de patrones a los LEDs de la placa.
      // Incrementa el contador para el siguiente paso de la animación.
      // Si llega a 16 (final de la matriz), vuelve a 0 (reinicio de secuencia).
      if (++cnt >= sizeof(led_val)) {
        cnt = 0U;
      }
    }
    osDelay (100);
  }
}

/**
  * @brief Este hilo se encarga de actualizar el LCD cada segundo y de hacer el parpadeo de 5 segundos si la alarma salta.
  */
void Time_Thread (void *argument) {
  MSGQUEUE_OBJ_LCD_t msg_lcd;
  char t_buffer[20], d_buffer[20];
  uint32_t tick_inicio_alarma = 0;
  uint8_t estado_alarma = 0; // 0: reposo, 1: parpadeando

  // 1. Inicializamos el reloj y la alarma nada más arrancar el hilo
  RTC_Init();
  RTC_SetAlarm_EveryMinute();

  while (1) {
    // 2. LEER HORA Y FECHA
    RTC_GetTimeDate(t_buffer, d_buffer);

    // 3. ENVIAR AL LCD (Línea 1: Hora, Línea 2: Fecha)
    memset(&msg_lcd, 0, sizeof(msg_lcd));
    strcpy(msg_lcd.Lin1, t_buffer); 
    strcpy(msg_lcd.Lin2, d_buffer); 
    osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0);

    // 4. GESTIÓN DEL PARPADEO (LD1 - LED VERDE)
    if (alarm_triggered) {
      alarm_triggered = 0;
      estado_alarma = 1; // Empezamos el parpadeo
      tick_inicio_alarma = osKernelGetTickCount(); // Guardamos el momento exacto
    }

    if (estado_alarma == 1) {
      HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); // Cambiamos estado del LED Verde (PB0)
      
      // Si han pasado 5000 milisegundos desde el inicio
      if ((osKernelGetTickCount() - tick_inicio_alarma) > 5000) {
        estado_alarma = 0;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); // Apagamos al terminar
      }
    }

    // Usamos un delay de 200ms para que el parpadeo sea rápido (5 veces por segundo)
    // y para que el reloj se actualice con fluidez en el LCD.
    osDelay(200); 
  }
}


///*----------------------------------------------------------------------------
//  Main Thread 'main': Run Network
// *---------------------------------------------------------------------------*/
__NO_RETURN void app_main (void *arg) {
  (void)arg;

  // 1. Inicialización de periféricos físicos
  LED_Initialize();
  Init_ThLCD(); // Crea el hilo servidor del LCD y su cola IPC
	
  // 2. Inicialización del ADC (Potenciómetro)
  ADC1_pins_F429ZI_config(); 
  ADC_Init_Single_Conversion(&hadc1, ADC1); 
	
  // 3. Inicialización de la pila de red (Arranca el Servidor Web)
  netInitialize (); 

  // 4. Lanzamiento de hilos de usuario
  osThreadNew (BlinkLed, NULL, NULL);   // Hilo para LEDs Práctica 1
  osThreadNew (Time_Thread, NULL, NULL); // Hilo para RTC Práctica 2

  osThreadExit();
}


