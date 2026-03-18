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
#include "power.h"

/* --- CONFIGURACIÓN DEL HILO PRINCIPAL (app_main) --- */
// Reservo una pila estática para el hilo del servidor. 
// Hacerlo estático me evita usar 'malloc' y previene problemas de fragmentación de memoria.
#define APP_MAIN_STK_SZ (2048U)        
uint64_t app_main_stk[APP_MAIN_STK_SZ / 8];
const osThreadAttr_t app_main_attr = {
  .stack_mem  = &app_main_stk[0],
  .stack_size = sizeof(app_main_stk)
};

/* --- VARIABLES GLOBALES COMPARTIDAS CON LA WEB --- */
// Estas variables me permiten comunicar la interfaz web con el hardware
bool LEDrun;                                // Controla si permito el parpadeo de LEDs desde el navegador.
char lcd_text[2][20+1];                     // Array 2D para guardar las dos líneas de texto que me mandan a la pantalla LCD.
uint8_t iniciar_parpadeo_sntp = 0;          // Bandera que levanto cuando recibo la hora de internet para disparar el LED rojo.
uint32_t contador_sntp_segundos = 0; // Lleva la cuenta de los segundos desde el arranque para saber cuándo pedir la hora a internet.

/* --- VARIABLES PARA EL APARTADO 5 (Configuración Web) --- */
uint8_t sntp_server_index = 0;              // Índice para seleccionar cuál de los dos servidores NTP estoy utilizando (0 o 1).
const char* sntp_servers[] = {"Google NTP (216.239.35.0)", "Cloudflare NTP (162.159.200.1)"}; // Nombres para mostrar en la web.
const char* sntp_ips[] = {"216.239.35.0", "162.159.200.1"}; // IPs reales a las que me conectaré.

RTC_PeriodoAlarma_t periodo_seleccionado = ALARMA_CADA_1_MIN; // Periodo por defecto de mi alarma.
uint8_t alarma_habilitada_web = 1;          // Bandera para saber si el usuario ha activado el checkbox de la alarma en la web.

/* --- RECURSOS DE PERIFÉRICOS Y RED --- */
ADC_HandleTypeDef hadc1;                    // Estructura de control (Handler) para mi conversor Analógico-Digital.
static NET_ADDR server_addr;                // Estructura estática para guardar la IP y puerto del servidor SNTP sin perderla en memoria.

/* --- RECURSOS MODOS BAJO CONSUMO --- */
// Variable global para que el usuario elija su política de ahorro
// 0 = Sleep (Despertar instantáneo), 1 = STOP (Ahorro máximo de pilas)
uint8_t modo_energia_seleccionado = 1; // Por defecto STOP para el SECRM


/* --- MIS RECURSOS DE TIMERS DEL RTOS --- */
// Guardo el identificador (ID) de mis timers para poder arrancarlos y pararlos a placer.
osTimerId_t timer_led_rojo;
osTimerId_t timer_led_verde;

// Funciones de reseteo que he implementado en stm32f4xx_it.c para limpiar la cuenta de parpadeos
extern void ResetPulsosRojo(void);
extern void ResetPulsosVerde(void);

/* --- DECLARACIÓN DE LAS FUNCIONES EXTERNAS --- */
extern void TimerRojo_Callback (void *argument);
extern void TimerVerde_Callback (void *argument);

/* Thread declarations */
void Time_Thread (void *argument);
void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion);
__NO_RETURN void app_main (void *arg);


/**
 * @brief Lee mi potenciómetro usando el canal 10 del ADC1.
 * @param ch : Canal (se mantiene por compatibilidad, aunque fuerzo el 10).
 * @return uint16_t : Valor analógico de 12 bits (0-4095).
 */
uint16_t AD_in (uint32_t ch) {
  ADC_ChannelConfTypeDef sConfig = {0};
  
  sConfig.Channel = ADC_CHANNEL_10; 
  sConfig.Rank = 1;    
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1); 
  
  // Le doy 10ms al hardware para que termine su trabajo.
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  return 0; 
}

// Funciones vacías requeridas por la arquitectura de red de Keil.
uint8_t get_button (void) { return 0; }
void netDHCP_Notify (uint32_t if_num, uint8_t option, const uint8_t *val, uint32_t len) { }

/**
 * @brief Hilo dedicado exclusivamente al parpadeo rápido del LED verde.
 * Lo lanzo como un hilo independiente para que sea totalmente ajeno a otras tareas.
 */
void LedVerde_Sleep_Thread (void *argument) {
  while (1) {
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); 
    osDelay(100); 
  }
}

/**
 * @brief El "cerebro" temporal del sistema.
 * Aquí controlo la hora, el refresco de la pantalla y la entrada al modo Sleep.
 */
void Time_Thread (void *argument) {
  MSGQUEUE_OBJ_LCD_t msg_lcd;      
  char t_buffer[20], d_buffer[20]; 
  uint8_t divisor_100ms = 0;       
  uint32_t contador_sleep = 0;
    
  // 1. Pongo en marcha mi hardware de tiempo.
  RTC_Init();
  RTC_ConfigurarAlarma(periodo_seleccionado);

  // 2. Configuro el botón azul (PC13) para despertarme del modo Sleep.
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  // 3. Configuro los LEDs de la placa (Verde=PB0, Rojo=PB14).
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  while (1) {
    // Mi hilo despierta cada 100ms para revisar tareas pendientes.
    osDelay(100); 

    // --- SI SALTÓ LA ALARMA DEL RTC ---
    if (alarma_activada == 1) {
        alarma_activada = 0; 
        if (alarma_habilitada_web == 1) {
            // Reinicio el timer del LED verde para que parpadee limpiamente.
            osTimerStop(timer_led_verde); 
            ResetPulsosVerde();           
            osTimerStart(timer_led_verde, 200U); 
        }
    }

    // --- SI RECIBÍ HORA DE INTERNET ---
    if (iniciar_parpadeo_sntp == 1) {
        iniciar_parpadeo_sntp = 0; 
        osTimerStop(timer_led_rojo); 
        ResetPulsosRojo();         
        osTimerStart(timer_led_rojo, 100U); // Ráfaga a 5Hz.
    }

    // --- LÓGICA DE 1 SEGUNDO ---
    divisor_100ms++;
    if (divisor_100ms >= 10) {
        divisor_100ms = 0; 
            
        // Si pasan 15 segundos sin actividad, mando el micro a dormir.
        contador_sleep++;
        if (contador_sleep == 15) {
					// En lugar de llamar a una función fija, elijo según mi variable global.
                switch(modo_energia_seleccionado) {
                    case 0:
                        Sistema_EntrarEnSleep();
                        break;
                    case 1:
                        Sistema_EntrarEnSleep();
                        break;
                    default:
                        Sistema_EntrarEnSleep();
                } 
        }
        
        // Actualizo la pantalla LCD con la hora y fecha actuales.
        RTC_ObtenerHoraFecha(t_buffer, d_buffer);
        memset(&msg_lcd, 0, sizeof(msg_lcd));
        strcpy(msg_lcd.Lin1, t_buffer); 
        strcpy(msg_lcd.Lin2, d_buffer); 
        osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0); 
        
        // Gestiono la petición SNTP cada 3 minutos (o a los 5s de arrancar).
        contador_sntp_segundos++;
        if (contador_sntp_segundos == 5 || (contador_sntp_segundos > 5 && (contador_sntp_segundos % 180 == 0))) {
            server_addr.addr_type = NET_ADDR_IP4;
            server_addr.port = 0; 
            netIP_aton(sntp_ips[sntp_server_index], NET_ADDR_IP4, server_addr.addr);
            netSNTPc_GetTime(&server_addr, Sincronizacion_SNTP_Completada);
        }
    }
  }
}

/**
 * @brief Se ejecuta cuando el servidor NTP nos responde.
 * Traduzco el tiempo Unix al formato que entiende mi RTC.
 */
void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion) {
    if (segundos_unix > 0) {
        RTC_ActualizarDesdeUnix(segundos_unix);
        iniciar_parpadeo_sntp = 1; 
    }
}

/**
 * @brief Hilo inicial de arranque de la aplicación.
 */
__NO_RETURN void app_main (void *arg) {
  (void)arg;

  // 1. Despierto todos mis periféricos.
  LED_Initialize();
  Init_ThLCD(); 
  ADC1_pins_F429ZI_config(); 
  ADC_Init_Single_Conversion(&hadc1, ADC1); 
    
  // 2. Arranco el Stack de Red.
  netInitialize (); 

  // 3. Preparo mis timers para los LEDs.
  timer_led_rojo = osTimerNew(TimerRojo_Callback, osTimerPeriodic, NULL, NULL);
  timer_led_verde = osTimerNew(TimerVerde_Callback, osTimerPeriodic, NULL, NULL);

  // 4. Lanzo mis hilos de trabajo.
  osThreadNew (Time_Thread, NULL, NULL); 
  osThreadNew (LedVerde_Sleep_Thread, NULL, NULL); 
  
  // 5. Suicidio este hilo para liberar RAM, ya no hace falta.
  osThreadExit();
}
