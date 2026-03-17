#ifndef __POWER_H
#define __POWER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

// Bandera global para saber el estado del sistema
extern volatile uint8_t is_sleeping;

void Sistema_EntrarEnSleep(void);

#endif
