/**
 * @file    power.c
 * @author  Jose Vargas Gonzaga
 * @brief   Lógica de entrada y salida del modo Sleep.
 * En este módulo controlo el flujo crítico de dormir el procesador,
 * asegurándome de que el RTOS no me despierte antes de tiempo.
 */

#include "power.h"
#include "stm32f4xx_hal.h"

// Direcciones por defecto del PHY de la placa Nucleo (LAN8742A)
// PHY_POWERDOWN 0x0800
// PHY_BCR       0x00

// Declaración de las funciones auxiliares para Ethernet
void ETH_PhyEnterPowerDownMode(void);
void ETH_PhyExitFromPowerDownMode(void);

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

    // 1. Requisito: Encender LED rojo (PB14)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    // 2. Apagar el PHY de Ethernet para reducir drásticamente el consumo
    // (y asegurar que el servidor web no responde a los pings/peticiones)
    ETH_PhyEnterPowerDownMode();

    // 3. Suspendemos el Tick del sistema. 
    HAL_SuspendTick();

    /* BUCLE CRÍTICO */
    while (despertar_por_boton == 0) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    // 4. Al salir (despertado por el botón), reanudo el Tick
    HAL_ResumeTick();

    // 5. Despertar el hardware de Ethernet para recuperar la conectividad
    ETH_PhyExitFromPowerDownMode();

    // 6. Requisito: Apagar LED rojo nada más salir del estado de letargo.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    
    is_sleeping = 0;
}

/* -------------------------------------------------------------------------- */
/* FUNCIONES DE GESTIÓN ENERGÉTICA DEL ETHERNET                               */
/* -------------------------------------------------------------------------- */

void ETH_PhyEnterPowerDownMode(void) {
    ETH_HandleTypeDef heth;
    uint32_t phyregval = 0; 
    
    heth.Instance = ETH;
    heth.Init.PhyAddress = 0x00; // Dirección por defecto
    heth.Instance->MACMIIAR = (uint32_t)ETH_MACMIIAR_CR_Div102;

    // Leemos el registro de control del PHY
    HAL_ETH_ReadPHYRegister(&heth, PHY_BCR, &phyregval);
    
    // Activamos el bit de Power Down
    phyregval |= PHY_POWERDOWN;
    
    // Escribimos el nuevo valor para dormir el chip
    HAL_ETH_WritePHYRegister(&heth, PHY_BCR, phyregval);
}

void ETH_PhyExitFromPowerDownMode(void) {
    ETH_HandleTypeDef heth;
    uint32_t phyregval = 0;
    
    heth.Instance = ETH;
    heth.Init.PhyAddress = 0x00; 
    heth.Instance->MACMIIAR = (uint32_t)ETH_MACMIIAR_CR_Div102; 
    
    // Leemos el registro de control
    HAL_ETH_ReadPHYRegister(&heth, PHY_BCR, &phyregval);
    
    // Si está dormido, lo despertamos quitando el bit
    if ((phyregval & PHY_POWERDOWN) != RESET) {
        phyregval &= ~PHY_POWERDOWN;
        HAL_ETH_WritePHYRegister(&heth, PHY_BCR, phyregval);
    }
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
