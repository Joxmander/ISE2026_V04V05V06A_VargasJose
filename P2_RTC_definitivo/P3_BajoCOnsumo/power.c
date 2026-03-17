#include "power.h"
#include "cmsis_os2.h"


volatile uint8_t is_sleeping = 0; // Arrancamos despiertos

void Sistema_EntrarEnSleep(void) {
    is_sleeping = 1; // Avisamos de que nos vamos a dormir

    // Encendemos el LED rojo antes de dormir
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

	
    // Suspendemos el RTOS para que no nos despierte cada milisegundo
    HAL_SuspendTick();

    // Entramos en modo Sleep
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);

    // --- EL MICRO SE CONGELA AQUÍ HASTA PULSAR EL BOTÓN ---

    // Al despertar, reanudamos el RTOS
    HAL_ResumeTick();

    // Apagamos el LED rojo nada más salir
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    
    is_sleeping = 0; // Avisamos de que volvemos a estar despiertos
}
