#include "power.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"

// Usamos esta bandera para saber si el botón azul nos ha despertado realmente
volatile uint8_t despertar_por_boton = 0;
volatile uint8_t is_sleeping; 

void Sistema_EntrarEnSleep(void) {
    is_sleeping = 1;
    despertar_por_boton = 0;

    // 1. Requisito: Encender LED rojo (PB14) antes de entrar
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    // 2. Suspendemos el Tick del sistema para que el RTOS no nos despierte cada 1ms
    HAL_SuspendTick();

    /* BUCLE CRÍTICO: Si el micro despierta por red u otra cosa que no sea el botón,
       se vuelve a dormir automáticamente. */
    while (despertar_por_boton == 0) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    // 3. Al salir (despertado por botón), reanudamos el Tick
    HAL_ResumeTick();

    // 4. Requisito: Apagar LED rojo nada más salir
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    
    is_sleeping = 0;
}
