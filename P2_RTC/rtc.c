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
#include <time.h>		//Libreria estandar del tiempo

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

/**
 * @brief Convierto los segundos que me manda internet a hora real y actualizo mi RTC
 */
void RTC_ActualizarDesdeUnix(uint32_t segundos_unix) {
    // 1. CORRECCIÓN DEL EFECTO 1900
    // Si el servidor envía los segundos desde 1900 en lugar de 1970, el número es inmenso.
    // Le restamos 70 años en segundos para normalizarlo al estándar UNIX.
    if (segundos_unix > 2208988800UL) {
        segundos_unix -= 2208988800UL;
    }

    // 2. HUSO HORARIO: Sumamos 1 hora (3600 segundos) por estar en España (UTC+1)
    uint32_t tiempo_local = segundos_unix + 3600;

    // 3. EXTRAER HORA, MINUTOS Y SEGUNDOS
    uint32_t segundos_hoy = tiempo_local % 86400; // Segundos transcurridos solo en el día de hoy
    
    RTC_TimeTypeDef sTime = {0};
    sTime.Hours   = segundos_hoy / 3600;
    sTime.Minutes = (segundos_hoy % 3600) / 60;
    sTime.Seconds = segundos_hoy % 60;
    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

    // 4. EXTRAER FECHA MATEMÁTICAMENTE (¡Adiós gmtime!)
    uint32_t dias_totales = tiempo_local / 86400;
    uint32_t anio = 1970;

    // Calculamos el año restando días (teniendo en cuenta los bisiestos)
    while (1) {
        uint32_t dias_este_anio = ((anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0)) ? 366 : 365;
        if (dias_totales >= dias_este_anio) {
            dias_totales -= dias_este_anio;
            anio++;
        } else {
            break;
        }
    }

    // Calculamos el mes
    uint8_t mes = 1;
    uint32_t dias_por_mes[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0)) {
        dias_por_mes[1] = 29; // Corregir Febrero si es bisiesto
    }

    for (int i = 0; i < 12; i++) {
        if (dias_totales >= dias_por_mes[i]) {
            dias_totales -= dias_por_mes[i];
            mes++;
        } else {
            break;
        }
    }

    // El resto de días es el día del mes
    uint8_t dia = dias_totales + 1;

    // Calculamos el Día de la semana (El 1 Ene de 1970 fue Jueves = 4)
    uint8_t dia_semana = (( (tiempo_local / 86400) + 3 ) % 7 ) + 1; // 1=Lunes, 7=Domingo

    // 5. ESCRIBIR EN EL HARDWARE
    RTC_DateTypeDef sDate = {0};
    sDate.WeekDay = dia_semana;
    sDate.Month   = mes;
    sDate.Date    = dia;
    sDate.Year    = anio - 2000; // Al hardware solo le guardamos las últimas 2 cifras (2026 -> 26)
    
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

/**
 * @brief Para probar si mi SNTP funciona y me corrige, pongo el reloj a las 00:00 del 01/01/2000.
 */
void RTC_Reset_A_2000(void) {
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  // Pongo la hora completamente a cero
  sTime.Hours = 0;
  sTime.Minutes = 0;
  sTime.Seconds = 0;
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

  // Pongo la fecha al 1 de enero de 2000 (históricamente cayó en sábado)
  sDate.WeekDay = RTC_WEEKDAY_SATURDAY; 
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 1;
  sDate.Year = 0; // El año 2000 es el 00 para este reloj
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

