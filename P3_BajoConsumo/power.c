/**
 * @file    power.c
 * @author  Jose Vargas Gonzaga
 * @brief   Lógica de entrada y salida del modo Sleep.
 * En este módulo controlo el flujo crítico de dormir el procesador,
 * asegurándome de que el RTOS no me despierte antes de tiempo.
 */

#include "power.h"
#include "stm32f4xx_hal.h"

// Necesitamos acceder a la configuración de reloj para restaurarla
extern void SystemClock_Config(void); 

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


/**
 * @brief Pone el procesador en modo STOP para maximizar el ahorro energético.
 * En este modo detengo casi todos los relojes internos (incluido el PLL) pero 
 * mantengo la RAM intacta para no perder el estado del servidor web.
 * Al despertar, el sistema vuelve por defecto a 16MHz (HSI), por lo que 
 * reconfiguro manualmente el reloj para volver a los 180MHz necesarios.
 */
void Sistema_EntrarEnStop(void) {
    is_sleeping = 1;
    despertar_por_boton = 0;

    // Indicador visual: LED Rojo encendido
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    // 1. Suspender el tick de HAL
    HAL_SuspendTick();

    // 2. Entrar en modo STOP
    // Usamos el regulador de bajo consumo para ahorrar aún más
	  // Lo despertamos con la interrupcion del PC13, pero podemos añadir la que queramos
    while (despertar_por_boton == 0) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    /* --- EL MICRO SE DETIENE AQUÍ HASTA QUE PULSE EL BOTÓN --- */

    // 3. ¡IMPORTANTE! Al despertar de STOP, el sistema usa HSI (16MHz).
    // Debemos reactivar el HSE y el PLL para volver a 180MHz.
    SystemClock_Config();

    // 4. Reanudar el tick de HAL
    HAL_ResumeTick();

    // Apagar LED rojo al salir
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    
    is_sleeping = 0;
}
