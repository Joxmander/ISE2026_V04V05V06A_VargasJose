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

    // 3. Apagamos periféricos internos TEMPORALMENTE (Dinámico)
    Apagar_Perifericos_Temporalmente();
	
    // 4. Suspendemos el Tick del sistema. 
    HAL_SuspendTick();

    /* BUCLE CRÍTICO */
    while (despertar_por_boton == 0) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    // 5. Al salir (despertado por el botón), reanudo el Tick
    HAL_ResumeTick();
		
    // 6. Restauramos periféricos internos TEMPORALMENTE (Dinámico)
    Restaurar_Perifericos_Temporalmente();

    // 7. Despertar el hardware de Ethernet para recuperar la conectividad
    ETH_PhyExitFromPowerDownMode();

    // 8. Requisito: Apagar LED rojo nada más salir del estado de letargo.
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

    // 1. Indicador visual: LED Rojo encendido
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);

    // 2. Apagamos el hardware de red (Vital para el consumo en batería)
    ETH_PhyEnterPowerDownMode();

	  // 3. Apagamos periféricos internos TEMPORALMENTE (Dinámico)
    Apagar_Perifericos_Temporalmente();
	
    // 4. Suspendemos el tick de HAL para que el RTOS no nos despierte por error
    HAL_SuspendTick();

    // 5. Entrar en modo STOP.
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

    // 7. Restauramos periféricos internos TEMPORALMENTE (Dinámico)
    Restaurar_Perifericos_Temporalmente();

    // 8. Despertamos el PHY de Ethernet para recuperar la red
    ETH_PhyExitFromPowerDownMode();

    // 9. Apagar LED rojo al salir
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



/* -------------------------------------------------------------------------- */
/* FUNCIONES DE OPTIMIZACIÓN DE PINES (BASADO EN LAS DIAPOSITIVAS DE CLASE)   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configura pines no utilizados a modo analógico y apaga relojes innecesarios.
 * Esto elimina las corrientes de fuga (floating leakage) antes de dormir.
 */
void Optimizar_Hardware_Bajo_Consumo(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Apagamos relojes de periféricos pesados que seguro no usamos (USB, Random Number Gen)
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
    __HAL_RCC_RNG_CLK_DISABLE();

    // Configuramos la estructura para modo analógico (cero fugas)
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    /* --- PUERTO D --- 
       Solo usamos el PD14 para el LCD (CS). 
       El símbolo '~' invierte los bits. Significa: "Todos los pines MENOS el 14". */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_All & ~(GPIO_PIN_14);
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* --- PUERTO F --- 
       Solo usamos el PF13 para el LCD (A0). */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_All & ~(GPIO_PIN_13);
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* --- PUERTO E --- 
       No lo usamos para nada en esta práctica. Lo ponemos entero en analógico. */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_All;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    
    // Y como el Puerto E no se usa, le cortamos el reloj para ahorrar aún más.
    __HAL_RCC_GPIOE_CLK_DISABLE();

    /* NOTA: No tocamos los Puertos A, B y C porque contienen la red Ethernet, 
       los LEDs, el ADC y, lo más importante, el PC13 que nos tiene que despertar. */
}

/**
 * @brief Apaga temporalmente los relojes de los periféricos que usamos en modo RUN,
 * pero que son inútiles mientras el sistema duerme. (Optimización Dinámica)
 */
void Apagar_Perifericos_Temporalmente(void) {
    // Apagamos el reloj del ADC1 (No vamos a leer el potenciómetro mientras dormimos)
    __HAL_RCC_ADC1_CLK_DISABLE();
    
    // Apagamos el reloj del Timer 7 (El que usas para los delays del LCD)
    __HAL_RCC_TIM7_CLK_DISABLE();
    
    // Apagamos el reloj del SPI1 (No vamos a mandar datos a la pantalla dormidos)
    __HAL_RCC_SPI1_CLK_DISABLE();
}

/**
 * @brief Restaura la energía de los periféricos para que el sistema vuelva a funcionar normal.
 */
void Restaurar_Perifericos_Temporalmente(void) {
    // Volvemos a darles vida en el mismo orden
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_TIM7_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
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

