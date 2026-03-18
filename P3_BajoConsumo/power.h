/**
 * @file    power.h
 * @author  Jose Vargas Gonzaga
 * @brief   Definiciones para la gestión del bajo consumo (Modo Sleep).
 * Aquí declaro las variables y funciones que me permiten dormir el micro
 * y controlar su estado desde otros módulos.
 */

#ifndef __POWER_H
#define __POWER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* --- VARIABLES DE ESTADO --- */
// Uso 'volatile' porque estas banderas cambian dentro de interrupciones
// y quiero que el compilador siempre lea el valor real de la RAM.
extern volatile uint8_t is_sleeping;

/* --- PROTOTIPOS --- */
/**
 * @brief Configura el sistema para entrar en modo de bajo consumo (Sleep).
 */
void Sistema_EntrarEnSleep(void);

/**
 * @brief Configura el sistema para entrar en modo Stop.
 */
void Sistema_EntrarEnStop(void);

/**
 * @brief Configura el sistema para entrar en modo Standby.
 */
void Sistema_EntrarEnStandby(void);

#endif
