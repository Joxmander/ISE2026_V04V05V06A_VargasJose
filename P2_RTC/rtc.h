#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx_hal.h"

/* Fechas por defecto de la práctica */
#define PRACTICA_HORA    0x09
#define PRACTICA_MIN     0x00
#define PRACTICA_SEG     0x00
#define PRACTICA_DIA     0x06
#define PRACTICA_MES   RTC_MONTH_MARCH
#define PRACTICA_YEAR    0x26  /* Año 2026 */

/* Funciones para el Apartado 1 y 2 */
void RTC_Init(void);
void RTC_ObtenerHoraFecha(char *timeStr, char *dateStr);
void RTC_PonerAlarma_CadaMinuto(void);

/* Bandera para la alarma (se activa en la interrupción) */
extern uint8_t alarma_activada;

/* Funciones para el Apartado 4 */
void RTC_ActualizarDesdeUnix(uint32_t segundos_unix);
void RTC_Reset_A_2000(void);
#endif
