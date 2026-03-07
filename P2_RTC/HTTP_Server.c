/**
  ******************************************************************************
  * @file    HTTP_Server.c
  * @author  Jose Vargas Gonzaga
  * @brief   Implementación del servidor HTTP
  * Contiene la lógica principal para atender las peticiones de la
  * red y servir las páginas estáticas y dinámicas. Este módulo
  * coordina las llamadas a CGI, gestión de sockets y mantenimiento
  * de la conexión con el hardware (ADC, RTC, etc.).
  ******************************************************************************
  */
	
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

/* --- CONFIGURACIÓN DEL HILO PRINCIPAL (app_main) --- */
// Main stack size must be multiple of 8 Bytes
#define APP_MAIN_STK_SZ (2048U)		// Memoria asignada al hilo de la red, antes 1024
uint64_t app_main_stk[APP_MAIN_STK_SZ / 8];
const osThreadAttr_t app_main_attr = {
  .stack_mem  = &app_main_stk[0],
  .stack_size = sizeof(app_main_stk)
};

/* --- VARIABLES GLOBALES COMPARTIDAS CON LA WEB --- */
bool LEDrun;               					// Controla si el parpadeo de LEDs está activo
char lcd_text[2][20+1];    					// Almacena el texto que el usuario escribe en la web
uint8_t iniciar_parpadeo_sntp = 0;	// Variable global para avisar a mi hilo de que la sincronización fue un éxito

/* --- VARIABLES PARA EL APARTADO 5 (OPCIONAL) --- */
// Índice del servidor SNTP seleccionado (0 o 1)
uint8_t sntp_server_index = 0; 

// Para mostrar los nombres de cara al usuario en la página Web
const char* sntp_servers[] = {"Google NTP (216.239.35.0)", "Cloudflare NTP (162.159.200.1)"};
// IPs reales que Keil necesita para conectarse
const char* sntp_ips[] = {"216.239.35.0", "162.159.200.1"};

// Periodo de la alarma seleccionado desde la web
RTC_PeriodoAlarma_t periodo_seleccionado = ALARMA_CADA_1_MIN;
// Bandera para saber si el usuario quiere la alarma encendida o apagada
uint8_t alarma_habilitada_web = 1;

extern osThreadId_t TID_Display;
extern osThreadId_t TID_Led;

/* Handler del ADC (Potenciómetro) */
ADC_HandleTypeDef hadc1;
													 
/* Thread IDs */
osThreadId_t TID_Display;
osThreadId_t TID_Led;

/* Thread declarations */
// static void BlinkLed (void *arg); // <-- COMENTADO PARA QUITAR EL WARNING
void Time_Thread (void *argument);

void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion);

__NO_RETURN void app_main (void *arg);

/*----------------------------------------------------------------------------
  Funciones de soporte para el Servidor Web (CGI)
 *---------------------------------------------------------------------------*/

/**
  * @brief Lee el valor del potenciómetro. 
  */
uint16_t AD_in (uint32_t ch) {
  ADC_ChannelConfTypeDef sConfig = {0};
  
  sConfig.Channel = ADC_CHANNEL_10; 
  sConfig.Rank = 1;	
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;	 
  
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  return 0;
}

/**
  * @brief Devuelve el estado de los botones.
  */
uint8_t get_button (void) {
  return 0; 
}

/* IP address change notification */
void netDHCP_Notify (uint32_t if_num, uint8_t option, const uint8_t *val, uint32_t len) {
  (void)if_num;
  (void)val;
  (void)len;

  if (option == NET_DHCP_OPTION_IP_ADDRESS) {
  }
}

/*----------------------------------------------------------------------------
  Thread 'BlinkLed': Blink the LEDs on an eval board
  (COMENTADO TEMPORALMENTE PARA ELIMINAR EL WARNING #177-D DE FUNCION NO USADA)
 *---------------------------------------------------------------------------*/
/*
static __NO_RETURN void BlinkLed (void *arg) {
  const uint8_t led_val[16] = { 0x48,0x88,0x84,0x44,0x42,0x22,0x21,0x11,
                                0x12,0x0A,0x0C,0x14,0x18,0x28,0x30,0x50 };
  uint32_t cnt = 0U;

  (void)arg;
  LEDrun = true;
  while(1) {
    if (LEDrun == true) {
      LED_SetOut (led_val[cnt]);
      if (++cnt >= sizeof(led_val)) {
        cnt = 0U;
      }
    }
    osDelay (100);
  }
}
*/

/**
 * @brief Este hilo gestiona mi reloj, alarmas, botón azul y las llamadas a SNTP.
 * Modificado para el Apartado 5: Ahora permite cambiar servidor y periodos.
 */
void Time_Thread (void *argument) {
  MSGQUEUE_OBJ_LCD_t msg_lcd;
  char t_buffer[20], d_buffer[20];
  
  uint32_t tick_inicio_alarma = 0;
  uint8_t estado_alarma = 0; 
  
  uint32_t tick_parpadeo_sntp = 0;
  uint8_t estado_parpadeo_sntp = 0;

  uint32_t contador_sntp_segundos = 0;
  uint32_t temporizador_hilo_100ms = 0;

  // 1. Inicializo mi reloj
  RTC_Init();
  RTC_ConfigurarAlarma(periodo_seleccionado);

  // 2. Configuro el pin de mi botón azul (PC13)
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // 3. Configuro el pin de mi LED Rojo (PB14) para SNTP
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  while (1) {
    // --- 1. REFRESCO DEL LCD Y GESTIÓN DE LA RED SNTP ---
    if (temporizador_hilo_100ms % 10 == 0) {
        
        RTC_ObtenerHoraFecha(t_buffer, d_buffer);
        memset(&msg_lcd, 0, sizeof(msg_lcd));
        strcpy(msg_lcd.Lin1, t_buffer); 
        strcpy(msg_lcd.Lin2, d_buffer); 
        osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0);
        
        contador_sntp_segundos++;
        
        // Sincronización inicial y periódica (cada 3 min = 180s)
        if (contador_sntp_segundos == 15 || (contador_sntp_segundos > 15 && (contador_sntp_segundos % 180 == 0))) {
            // ¡Novedad!: Convertimos la IP de texto a estructura de red
            NET_ADDR server_addr;
            server_addr.addr_type = NET_ADDR_IP4;
            server_addr.port = 0; // Usar puerto por defecto del SNTP (123)
            netIP_aton(sntp_ips[sntp_server_index], NET_ADDR_IP4, server_addr.addr);
            
            // Hacemos la llamada pasándole el puntero de la estructura
            netSNTPc_GetTime(&server_addr, Sincronizacion_SNTP_Completada);
        }
    }

    // --- 2. LECTURA DE MI BOTÓN AZUL (PC13) ---
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        RTC_Reset_A_2000(); 
    }

    // --- 3. GESTIÓN DEL PARPADEO SNTP (LED ROJO PB14) ---
    if (iniciar_parpadeo_sntp == 1) {
        iniciar_parpadeo_sntp = 0;
        estado_parpadeo_sntp = 1; 
        tick_parpadeo_sntp = osKernelGetTickCount(); 
    }

    if (estado_parpadeo_sntp == 1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_14); 
        if ((osKernelGetTickCount() - tick_parpadeo_sntp) > 4000) {
            estado_parpadeo_sntp = 0;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); 
        }
    }

    // --- 4. GESTIÓN DEL PARPADEO DE LA ALARMA (LED VERDE PB0) ---
    if (alarma_activada == 1 && alarma_habilitada_web == 1) {
        alarma_activada = 0;
        estado_alarma = 1; 
        tick_inicio_alarma = osKernelGetTickCount(); 
    } else if (alarma_activada == 1 && alarma_habilitada_web == 0) {
        alarma_activada = 0;
    }

    if (estado_alarma == 1) {
        if (temporizador_hilo_100ms % 2 == 0) {
            HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); 
        }
        
        if ((osKernelGetTickCount() - tick_inicio_alarma) > 5000) {
            estado_alarma = 0;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); 
        }
    }

    temporizador_hilo_100ms++;
    osDelay(100); 
  }
}

/**
 * @brief Esta es la función "callback" a la que el sistema de red llama automáticamente
 * cuando recibe por fin la respuesta del servidor de hora de internet.
 */
void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion) {
    if (segundos_unix > 0) {
        // Recibí una hora válida. Llamo a mi función para inyectarla en el hardware
        RTC_ActualizarDesdeUnix(segundos_unix);
        
        // Levanto mi bandera para decirle a mi hilo principal que empiece a parpadear el LED rojo
        iniciar_parpadeo_sntp = 1;
    }
}


/*----------------------------------------------------------------------------
  Main Thread 'main': Run Network
 *---------------------------------------------------------------------------*/
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
  //osThreadNew (BlinkLed, NULL, NULL);   // COMENTADO PORQUE NO SE USA AHORA
  osThreadNew (Time_Thread, NULL, NULL); // Hilo para RTC Práctica 2

  osThreadExit();
}
