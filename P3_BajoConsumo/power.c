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

    // 1. Indicador visual: LED Rojo encendido
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    // 2. Apagamos el hardware de red (Vital para el consumo en batería)
    ETH_PhyEnterPowerDownMode();

    // 3. Suspendemos el tick de HAL para que el RTOS no nos despierte por error
    HAL_SuspendTick();

    // 4. Entrar en modo STOP.
    // Usamos PWR_LOWPOWERREGULATOR_ON para ahorrar la máxima energía posible
    while (despertar_por_boton == 0) {
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    }

    /* --- EL MICRO SE DETIENE AQUÍ HASTA QUE PULSE EL BOTÓN --- */

    // 5. ¡CRÍTICO! Reanudar el tick ANTES de configurar el reloj.
    // Si no hacemos esto, SystemClock_Config() se quedará en un bucle infinito.
    HAL_ResumeTick();

    // 6. Al despertar, el sistema usa el reloj interno básico HSI (16MHz).
    // Reactivamos el HSE y el PLL para volver a los 180MHz.
    SystemClock_Config();

    // 7. Despertamos el PHY de Ethernet para recuperar la red
    ETH_PhyExitFromPowerDownMode();

    // 8. Apagar LED rojo al salir
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    
    is_sleeping = 0;
}


/**
 * @brief Pone el procesador en modo STANDBY (Apagado profundo de emergencia).
 * Ideal para el "Modo Batería Crítica" del Nodo B del SECRM.
 * ADVERTENCIA: Se pierde el contenido de la RAM. Al despertar (vía PA0 o Reset),
 * el sistema se reiniciará desde cero pasando por el main().
 */

/**
 * @brief Pone el procesador en modo STANDBY (Apagado profundo de emergencia).
 * Ideal para el "Modo Batería Crítica" del Nodo B del SECRM.
 * Al salir de este modo, el sistema se reiniciará completamente.
 */
void Sistema_EntrarEnStandby(void) {
    
    is_sleeping = 1; // Mantenemos coherencia con el resto del código

    // 1. Aviso visual: Encendemos el LED rojo 2 segundos antes de apagarnos.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
    HAL_Delay(2000); 
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);

    // 2. Apagamos dispositivos EXTERNOS (Cumpliendo la diapositiva de reducción de consumo)
    ETH_PhyEnterPowerDownMode();

    // 3. Habilitar el reloj del controlador de energía (PWR)
    __HAL_RCC_PWR_CLK_ENABLE();

    // 4. Limpiamos cualquier bandera de WakeUp antigua que haya quedado colgada
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

    // 5. Nos aseguramos de deshabilitar el pin PA0 para evitar reseteos por ruido electromagnético
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);

    // 6. Entramos en STANDBY absoluto.
    HAL_PWR_EnterSTANDBYMode();

    /* --- EL MICRO NUNCA PASARÁ DE ESTA LÍNEA ---
       Solo revivirá cuando el usuario pulse el botón físico de RESET */
}

