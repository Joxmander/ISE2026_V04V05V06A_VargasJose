/**
  ******************************************************************************
  * @file    rtc.c
  * @author  Jose Vargas Gonzaga
  * @brief   Implementación del módulo Real-Time Clock (RTC) para la Práctica 2.
  *          Este archivo contiene las funciones para inicializar el reloj interno,
  *          leer la hora/fecha y configurar alarmas periódicas por interrupción.
  * 
  * @details 
  * - Hardware: STM32F429ZI (NUCLEO-F429ZI)
  * - Fuente de reloj: LSI (Low Speed Internal) aprox. 32kHz.
  * - Formato: 24 Horas.
  * - Sincronización: Requiere lectura de Hora seguido de Fecha para desbloqueo.
  ******************************************************************************
  */

#include "rtc.h"
#include <stdio.h>

RTC_HandleTypeDef hrtc;
uint8_t alarma_activada = 0;

/**
  * @brief Configuración de Hardware del RTC (Reloj y Energía)
  */
void HAL_RTC_MspInit(RTC_HandleTypeDef* hrtc) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  // 1. Habilitar el reloj de Power y dar acceso al dominio de backup
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  // 2. Configurar el LSI (Oscilador interno lento) como fuente del RTC
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI;	//No necesitamos que la placa tenga un cristal externo soldado.
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;	//Ordeno encenderlo
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

  // 3. Habilitar el reloj del periférico RTC
  __HAL_RCC_RTC_ENABLE();

  // 4. Configurar interrupciones para la alarma
  HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
}

/**
  * @brief Inicializa el RTC con la fecha de la práctica
  */
void RTC_Init(void) {
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;   //RTC_HOURFORMAT_12
	// Si usamos el LSI (que va a 32768 Hz), tenemos que dividirlo:
  // La fórmula es: Frecuencia / ((Asynch + 1) * (Synch + 1))
  // 32768 / ((127 + 1) * (255 + 1)) = 32768 / (128 * 256) = 32768 / 32768 = 1 Hz.
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
	//Esto se usa para que el RTC saque pulsos por un pin externo, se usaría para calibrar con un osciloscopio
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;	
  HAL_RTC_Init(&hrtc);	//Llama automáticamente a HAL_RTC_MspInit.

 // --- CONFIGURACIÓN DE LA HORA INICIAL ---
	
	// Estructuras temporales para enviarle los datos al chip
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

	//Cargamos la hora inicial
  sTime.Hours = PRACTICA_HORA;
  sTime.Minutes = PRACTICA_MIN;
  sTime.Seconds = PRACTICA_SEG;
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD);

  sDate.WeekDay = RTC_WEEKDAY_FRIDAY;
  sDate.Month = PRACTICA_MES;
  sDate.Date = PRACTICA_DIA;
  sDate.Year = PRACTICA_YEAR;
	//BCD porque el hardware del RTC guarda los números en formato BCD
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD);
}

/**
  * @brief Lee hora y fecha del RTC y las guarda en strings para el LCD
  */
void RTC_ObtenerHoraFecha(char *timeStr, char *dateStr) {
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;

	//Siempre hay que leer primero el tiempo y luego la fecha
	// Usamos FORMAT_BIN para que nos devuelva numeros naturales (0-59)
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  sprintf(timeStr, "%02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
  sprintf(dateStr, "%02d/%02d/20%02d", sDate.Date, sDate.Month, sDate.Year);
}

/**
  * @brief Configura la alarma para que salte en el segundo 00 de cada minuto
  */
void RTC_PonerAlarma_CadaMinuto(void) {
  RTC_AlarmTypeDef sAlarm = {0};

  sAlarm.AlarmTime.Seconds = 0x00;	// Queremos que salte en el segundo 00
	// El objetivo son los segundos, asi que enmascaramos las horas, minutos y segundos
  sAlarm.AlarmMask = RTC_ALARMMASK_HOURS | RTC_ALARMMASK_MINUTES | RTC_ALARMMASK_DATEWEEKDAY;
  sAlarm.Alarm = RTC_ALARM_A;	//Tambien podemos usar otra simultaneamente, RTC_ALARM_B
  HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD);
}

