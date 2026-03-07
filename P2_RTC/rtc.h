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


/* Tipos de periodo de alarma */
typedef enum {
    ALARMA_DESACTIVADA = 0,
    ALARMA_CADA_10_SEG,
    ALARMA_CADA_1_MIN,
    ALARMA_CADA_5_MIN
} RTC_PeriodoAlarma_t;

/* Variables globales de control (externas para usarlas en el servidor web) */
extern RTC_PeriodoAlarma_t periodo_actual;
extern char* sntp_server_list[];
extern uint8_t sntp_server_index; // 0 o 1

/* Funciones para el Apartado 1, 2 y 3 */
void RTC_Init(void);
void RTC_ObtenerHoraFecha(char *timeStr, char *dateStr);
void RTC_PonerAlarma_CadaMinuto(void);

/* Bandera para la alarma (se activa en la interrupción) */
extern uint8_t alarma_activada;

/* Funciones para el Apartado 4 */
void RTC_ActualizarDesdeUnix(uint32_t segundos_unix);
void RTC_Reset_A_2000(void);

/* Funciones para el Apartado 5 */
void RTC_ConfigurarAlarma(RTC_PeriodoAlarma_t periodo);
void RTC_DesactivarAlarma(void);

#endif
