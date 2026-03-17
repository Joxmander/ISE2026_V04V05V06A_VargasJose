/**
 * @file    power.c
 * @author  Jose Vargas Gonzaga
 * @brief   Lógica de entrada y salida del modo Sleep.
 * En este módulo controlo el flujo crítico de dormir el procesador,
 * asegurándome de que el RTOS no me despierte antes de tiempo.
 */

#include "power.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"

// Esta bandera la pongo a 1 en la interrupción del botón azul (PC13)
// para confirmar que el despertar es legítimo y no por un ruido o red.
volatile uint8_t despertar_por_boton = 0;
volatile uint8_t is_sleeping; 

/**
 * @brief Detiene la ejecución del CPU y espera a que pulse el botón azul.
 * Sigo un orden estricto: encender aviso visual, parar el corazón del RTOS (Tick)
 * y entrar en un bucle WFI (Wait For Interrupt).
 */
void Sistema_EntrarEnSleep(void) {
    is_sleeping = 1;
    despertar_por_boton = 0;

    // 1. Requisito: Encender LED rojo (PB14) para avisar que me voy a dormir.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    // 2. Suspendemos el Tick del sistema. 
    // Si no hago esto, el RTOS generaría una interrupción cada 1ms y me despertaría.
    HAL_SuspendTick();

    /* BUCLE CRÍTICO: 
       Si el micro se despierta por la actividad de red (Ethernet) o el reloj (RTC),
       pero NO por el botón, el bucle lo vuelve a dormir inmediatamente. */
    while (despertar_por_boton == 0) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    // 3. Al salir (despertado por el botón azul), reanudo el Tick para que el RTOS vuelva a la vida.
    HAL_ResumeTick();

    // 4. Requisito: Apagar LED rojo nada más salir del estado de letargo.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    
    is_sleeping = 0;
}
