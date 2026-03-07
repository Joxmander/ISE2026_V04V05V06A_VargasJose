/**
  ******************************************************************************
  * @file    HTTP_Server.c
  * @author  Jose Vargas Gonzaga
  * @brief   Implementación del servidor HTTP
  *          Contiene la lógica principal para atender las peticiones de la
  *          red y servir las páginas estáticas y dinámicas. Este módulo
  *          coordina las llamadas a CGI, gestión de sockets y mantenimiento
  *          de la conexión con el hardware (ADC, RTC, etc.).
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
#include "lcd.h"

/* --- CONFIGURACIÓN DEL HILO PRINCIPAL (app_main) --- */
// Main stack size must be multiple of 8 Bytes
#define APP_MAIN_STK_SZ (2048U)		// Memoria asignada al hilo de la red, antes 1024
uint64_t app_main_stk[APP_MAIN_STK_SZ / 8];
const osThreadAttr_t app_main_attr = {
  .stack_mem  = &app_main_stk[0],
  .stack_size = sizeof(app_main_stk)
};

/* --- VARIABLES GLOBALES COMPARTIDAS CON LA WEB --- */
/* Estas variables son externas porque el archivo 'HTTP_Server_CGI.c' 
   las leerá o modificará cuando el usuario interactúe con el navegador. */
bool LEDrun;               					// Controla si el parpadeo de LEDs está activo
char lcd_text[2][20+1];    					// Almacena el texto que el usuario escribe en la web
uint8_t iniciar_parpadeo_sntp = 0;	// Variable global para avisar a mi hilo de que la sincronización fue un éxito
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

void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion);

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

///**
//  * @brief Este hilo se encarga de actualizar el LCD cada segundo y de hacer el parpadeo de 5 segundos si la alarma salta.
//  */
//void Time_Thread (void *argument) {
//  MSGQUEUE_OBJ_LCD_t msg_lcd;
//  char t_buffer[20], d_buffer[20];
//  uint32_t tick_inicio_alarma = 0;
//  uint8_t estado_alarma = 0; // 0: reposo, 1: parpadeando

//  // 1. Inicializamos el reloj y la alarma nada más arrancar el hilo
//  RTC_Init();
//  RTC_PonerAlarma_CadaMinuto();

//  while (1) {
//    // 2. LEER HORA Y FECHA
//    RTC_ObtenerHoraFecha(t_buffer, d_buffer);

//    // 3. ENVIAR AL LCD (Línea 1: Hora, Línea 2: Fecha)
//    memset(&msg_lcd, 0, sizeof(msg_lcd));
//    strcpy(msg_lcd.Lin1, t_buffer); 
//    strcpy(msg_lcd.Lin2, d_buffer); 
//    osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0);

//    // 4. GESTIÓN DEL PARPADEO (LD1 - LED VERDE)
//    if (alarma_activada) {
//      alarma_activada = 0;
//      estado_alarma = 1; // Empezamos el parpadeo
//      tick_inicio_alarma = osKernelGetTickCount(); // Guardamos el momento exacto
//    }

//    if (estado_alarma == 1) {
//      HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); // Cambiamos estado del LED Verde (PB0)
//      
//      // Si han pasado 5000 milisegundos desde el inicio
//      if ((osKernelGetTickCount() - tick_inicio_alarma) > 5000) {
//        estado_alarma = 0;
//        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); // Apagamos al terminar
//      }
//    }

//    // Usamos un delay de 200ms para que el parpadeo sea rápido (5 veces por segundo)
//    // y para que el reloj se actualice con fluidez en el LCD.
//    osDelay(200); 
//  }
//}

/**
 * @brief Este hilo gestiona mi reloj, alarmas, botón azul y las llamadas a SNTP.
 */
void Time_Thread (void *argument) {
  MSGQUEUE_OBJ_LCD_t msg_lcd;
  char t_buffer[20], d_buffer[20];
  
  // Variables para gestionar mi alarma (LED Verde en PB0)
  uint32_t tick_inicio_alarma = 0;
  uint8_t estado_alarma = 0; 
  
  // Variables para gestionar mi aviso visual de SNTP (LED Rojo en PB14)
  uint32_t tick_parpadeo_sntp = 0;
  uint8_t estado_parpadeo_sntp = 0;

  // Contadores de tiempo que he creado para controlar de forma precisa las peticiones
  uint32_t contador_sntp_segundos = 0;
  uint32_t temporizador_hilo_100ms = 0; // Me sirve para contar las vueltas de mi bucle while

  // 1. Inicializo mi reloj y configuro la interrupción de alarma del apartado anterior
  RTC_Init();
  RTC_PonerAlarma_CadaMinuto();

  // 2. Configuro el pin de mi botón azul (PC13) de la placa Nucleo para leerlo
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT; // Entrada
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // 3. Configuro el pin de mi LED Rojo (PB14) para el aviso de SNTP
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // Salida
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  while (1) {
    // --- 1. REFRESCO DEL LCD Y GESTIÓN DE LA RED SNTP ---
    // Como ahora duermo mi hilo 100ms (para lograr el parpadeo rápido de 5Hz), 
    // entro en este bloque solo 1 de cada 10 veces. Es decir: ¡exactamente 1 vez por segundo!
    if (temporizador_hilo_100ms % 10 == 0) {
        
        // Leo la hora actual de mi RTC y la mando al LCD
        RTC_ObtenerHoraFecha(t_buffer, d_buffer);
        memset(&msg_lcd, 0, sizeof(msg_lcd));
        strcpy(msg_lcd.Lin1, t_buffer); 
        strcpy(msg_lcd.Lin2, d_buffer); 
        osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0);
        
        // Incremento mi contador de segundos vitales para saber cuándo pedir la hora
        contador_sntp_segundos++;
        
        // Me exigen esperar al menos 15 segundos en el arranque para mi primera sincronización
        if (contador_sntp_segundos == 15) {
            // Pido la hora al servidor. Al pasar NULL usa el servidor por defecto que configuré en RTE
            netSNTPc_GetTime(NULL, Sincronizacion_SNTP_Completada);
        }
        // Luego, la práctica me exige re-sincronizar exactamente cada 3 minutos (180 segundos)
        else if (contador_sntp_segundos > 15 && (contador_sntp_segundos % 180 == 0)) {
            netSNTPc_GetTime(NULL, Sincronizacion_SNTP_Completada);
        }
//        else if (contador_sntp_segundos >= (180 + 15)) {
//            netSNTPc_GetTime(NULL, Sincronizacion_SNTP_Completada);
//            // Reseteo mi contador dejándolo en 15 para que el siguiente ciclo sea de 180 segundos exactos
//            contador_sntp_segundos = 15; 
//        }
    }

    // --- 2. LECTURA DE MI BOTÓN AZUL (PC13) ---
    // Si detecto que me están pulsando el botón (estado SET/ALTO)
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        RTC_Reset_A_2000(); // Llamo a mi función para mandar el reloj al pasado
    }

    // --- 3. GESTIÓN DEL PARPADEO SNTP (LED ROJO PB14) ---
    // Si mi callback de red me ha levantado la bandera avisando de que la hora llegó bien:
    if (iniciar_parpadeo_sntp == 1) {
        iniciar_parpadeo_sntp = 0; // Bajo mi bandera para que no entre en bucle
        estado_parpadeo_sntp = 1;  // Activo la máquina de estados del parpadeo
        tick_parpadeo_sntp = osKernelGetTickCount(); // Guardo el instante en el que empecé
    }

    if (estado_parpadeo_sntp == 1) {
        // Como mi hilo se ejecuta cada 100ms, al cambiar el estado del LED en cada vuelta 
        // consigo 100ms encendido y 100ms apagado (Periodo 200ms -> ¡Frecuencia de 5 Hz exacta!)
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_14); 
        
        // Me exigen que este castigo visual dure 4 segundos (4000 milisegundos)
        if ((osKernelGetTickCount() - tick_parpadeo_sntp) > 4000) {
            estado_parpadeo_sntp = 0; // Se acabó el tiempo, paro de parpadear
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); // Apago mi LED para dejarlo limpio
        }
    }

    // --- 4. GESTIÓN DEL PARPADEO DE LA ALARMA (LED VERDE PB0) ---
    if (alarma_activada == 1) {
        alarma_activada = 0;
        estado_alarma = 1; 
        tick_inicio_alarma = osKernelGetTickCount(); 
    }

    if (estado_alarma == 1) {
        // Mi antiguo código de la alarma iba a 200ms. Como ahora el hilo va el doble de rápido,
        // uso este pequeño truco matemático (%2) para que mi LED verde siga parpadeando a su ritmo original
        if (temporizador_hilo_100ms % 2 == 0) {
            HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); 
        }
        
        if ((osKernelGetTickCount() - tick_inicio_alarma) > 5000) {
            estado_alarma = 0;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); 
        }
    }

    // Incremento mi temporizador principal
    temporizador_hilo_100ms++;
    
    // Duermo mi hilo 100 milisegundos para dictar el ritmo de todo el sistema
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
  //osThreadNew (BlinkLed, NULL, NULL);   // Hilo para LEDs Práctica 1
  osThreadNew (Time_Thread, NULL, NULL); // Hilo para RTC Práctica 2

  osThreadExit();
}


