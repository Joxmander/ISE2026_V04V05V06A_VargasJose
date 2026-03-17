/**
  ******************************************************************************
  * @file    Templates/Src/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  *
  * @note    modified by ARM
  *          The modifications allow to use this file as User Code Template
  *          within the Device Family Pack.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT(c) 2017 STMicroelectronics</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */


/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
#include "rtc.h"
#include "power.h"

/* Private variables ---------------------------------------------------------*/
extern RTC_HandleTypeDef hrtc;
extern uint32_t contador_sntp_segundos;

// Variables de estado del sistema (usamos 'volatile' porque cambian en interrupciones)
extern volatile uint8_t despertar_por_boton;
extern volatile uint8_t is_sleeping;

// Traigo de forma externa los timers que he creado en mi HTTP_Server.c
// para poder detenerlos desde aquí cuando la secuencia de parpadeo termine.
extern osTimerId_t timer_led_rojo;
extern osTimerId_t timer_led_verde;

/* --- VARIABLES GLOBALES PARA LOS PULSOS --- */
// Llevan la cuenta de cuántas veces ha cambiado de estado cada LED
uint32_t pulsos_rojo = 0;
uint32_t pulsos_verde = 0;

/* ---  FUNCIONES DE RESETEO --- */

/**
 * @brief Pone a cero el contador de pulsos del LED rojo.
 * Esto me permite reiniciar la secuencia desde otros archivos de forma limpia
 * antes de disparar un nuevo evento SNTP.
 */
void ResetPulsosRojo(void) { 
    pulsos_rojo = 0; 
}

/**
 * @brief Pone a cero el contador de pulsos del LED verde.
 * Lo uso para preparar el terreno antes de que salte una nueva alarma del RTC.
 */
void ResetPulsosVerde(void) { 
    pulsos_verde = 0; 
}

/******************************************************************************/
/* Cortex-M4 Processor Exceptions Handlers                                    */
/******************************************************************************/

/**
 * @brief Manejador de la interrupción externa para los pines 10 al 15.
 * Aquí capturo físicamente la pulsación del botón azul de la placa (conectado a PC13).
 * El comportamiento cambia radicalmente dependiendo del estado de energía del sistema.
 */
void EXTI15_10_IRQHandler(void) {
    // Compruebo que la interrupción viene efectivamente del pin 13
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_13); // Limpio el flag para que no se dispare en bucle

        if (is_sleeping == 1) {
            // Si estábamos durmiendo, levanto la bandera para avisar a mi bucle
            // en 'power.c' de que el botón nos despertó legítimamente.
            despertar_por_boton = 1;
        } else {
            // Lógica normal de la Práctica 2 cuando el sistema está totalmente despierto.
            // Pulso el botón y reinicio la hora del sistema al año 2000.
            RTC_Reset_A_2000();
            contador_sntp_segundos = 0; // Reseteo el contador para retrasar la petición SNTP
        }
    }
}

/**
 * @brief Manejador físico de la interrupción de la Alarma del RTC.
 * Cuando el hardware del reloj detecta la coincidencia de tiempo, salta aquí.
 * Yo llamo a la función de la HAL para que ella se encargue de limpiar los flags
 * de bajo nivel y ejecute mi callback personalizado.
 */
void RTC_Alarm_IRQHandler(void) {
  HAL_RTC_AlarmIRQHandler(&hrtc);
}

/**
 * @brief Callback de la HAL que se ejecuta tras procesar la interrupción de alarma.
 * Aquí aplico mis filtros de software para decidir si realmente debo avisar
 * al sistema de la alarma, dependiendo de lo que haya configurado en la web.
 * @param hrtc : Puntero a la estructura del RTC.
 */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc) {
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    // Es obligatorio leer la hora y luego la fecha para desbloquear los registros
    // internos del hardware del RTC, según especifica el manual de ST.
    HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BIN);

    // FILTRO CRÍTICO DE ENERGÍA: Si el sistema está en modo Sleep, 
    // aborto la ejecución para no encender LEDs y ahorrar batería.
    if (is_sleeping == 1) {
        return; 
    }

    // Aplico matemáticas básicas para filtrar si la alarma debe sonar ahora:
    // Si el usuario me ha pedido 10 segundos, compruebo que el múltiplo coincida.
    if (periodo_actual == ALARMA_CADA_10_SEG) {
        if (sTime.Seconds % 10 != 0) return; 
    }
    // Si el usuario me ha pedido 5 minutos, exijo que el segundo sea 0 y el minuto sea múltiplo de 5.
    else if (periodo_actual == ALARMA_CADA_5_MIN) {
        if (sTime.Minutes % 5 != 0 || sTime.Seconds != 0) return; 
    }

    // Si la ejecución llega hasta aquí, ha superado todos los filtros.
    // Le levanto la bandera a mi hilo principal para que lance la secuencia visual.
    alarma_activada = 1; 
}

/**
 * @brief Callback del RTOS para controlar el parpadeo del LED ROJO (Sincronización SNTP).
 * El sistema operativo llamará a esta función automáticamente cada 100ms
 * porque así lo configuré al crear el timer en HTTP_Server.c.
 * @param argument : Puntero genérico del RTOS (no lo utilizo).
 */
void TimerRojo_Callback (void *argument) {

    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_14); // Cambio el estado del pin
    pulsos_rojo++;
    
    // Si he llegado a 40 cambios de estado (20 encendidos y 20 apagados)
    if (pulsos_rojo >= 40) {
        pulsos_rojo = 0; // Reseteo mi contador para la próxima vez
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); // Apago el LED por seguridad
        osTimerStop(timer_led_rojo); // Le digo al RTOS que detenga este timer temporalmente
    }
}

/**
 * @brief Callback del RTOS para controlar el parpadeo del LED VERDE (Alarma RTC).
 * El sistema operativo llamará a esta función automáticamente cada 200ms.
 * @param argument : Puntero genérico del RTOS (no lo utilizo).
 */
void TimerVerde_Callback (void *argument) {

    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); // Cambio el estado del pin
    pulsos_verde++;
    
    // Si he llegado a 25 cambios de estado
    if (pulsos_verde >= 25) {
        pulsos_verde = 0; // Lo preparo limpio para la siguiente alarma
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); // Aseguro que el LED quede apagado
        osTimerStop(timer_led_verde); // Detengo la ejecución del timer
    }
}


// ... (Resto de los handlers como NMI_Handler, HardFault_Handler, etc., quedan igual) ...


/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
#ifndef RTE_CMSIS_RTOS2_RTX5
void SVC_Handler(void)
{
}
#endif

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
#ifndef RTE_CMSIS_RTOS2_RTX5
void PendSV_Handler(void)
{
}
#endif

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
#ifndef RTE_CMSIS_RTOS2_RTX5
void SysTick_Handler(void)
{
  HAL_IncTick();
}
#endif

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/


/**
  * @}
  */ 

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
