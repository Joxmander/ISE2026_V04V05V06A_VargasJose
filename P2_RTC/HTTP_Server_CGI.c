/**
  ******************************************************************************
  * @file    HTTP_Server_CGI.c
  * @author  Jose Vargas Gonzaga
  * @brief   Módulo puente entre la Web y el Hardware (CGI).
  *          Aquí se procesan los formularios enviados por el usuario y se
  *          preparan los datos dinámicos (Hora, Voltaje) para mostrar en web.
  ******************************************************************************
  */

#include <stdio.h>
#include <string.h>
#include "cmsis_os2.h"
#include "rl_net.h"
#include "rtc.h"  				// Necesario para leer la hora
#include "lcd.h"          // Necesario para enviar mensajes al LCD
#include "Board_LED.h"    // ::Board Support:LED


#if      defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
#pragma  clang diagnostic push
#pragma  clang diagnostic ignored "-Wformat-nonliteral"
#endif

// Variables externas
extern uint16_t AD_in (uint32_t ch);
extern bool LEDrun;
extern char lcd_text[2][20+1];
extern uint8_t  get_button (void);

// Variables Locales.
static uint8_t P2;		// Variable local para guardar el estado de los 8 LEDs de la placa mbed
static uint8_t ip_addr[NET_ADDR_IP6_LEN];
static char    ip_string[40];

// My structure of CGI status variable.
typedef struct {
  uint8_t idx;
  uint8_t unused[3];
} MY_BUF;
#define MYBUF(p)        ((MY_BUF *)p)

/*----------------------------------------------------------------------------
  1. netCGI_ProcessQuery: Procesa datos enviados por la URL (GET)
  Normalmente se usa para configurar la IP desde la web. 
 *---------------------------------------------------------------------------*/
void netCGI_ProcessQuery (const char *qstr) {
  netIF_Option opt = netIF_OptionMAC_Address;
  int16_t      typ = 0;
  char var[40];

  do {
    // Loop through all the parameters
    qstr = netCGI_GetEnvVar (qstr, var, sizeof (var));
    // Check return string, 'qstr' now points to the next parameter

    switch (var[0]) {
      case 'i': // Local IP address
        if (var[1] == '4') { opt = netIF_OptionIP4_Address;       }
        else               { opt = netIF_OptionIP6_StaticAddress; }
        break;

      case 'm': // Local network mask
        if (var[1] == '4') { opt = netIF_OptionIP4_SubnetMask; }
        break;

      case 'g': // Default gateway IP address
        if (var[1] == '4') { opt = netIF_OptionIP6_DefaultGateway; }
        else               { opt = netIF_OptionIP6_DefaultGateway; }
        break;

      case 'p': // Primary DNS server IP address
        if (var[1] == '4') { opt = netIF_OptionIP4_PrimaryDNS; }
        else               { opt = netIF_OptionIP6_PrimaryDNS; }
        break;

      case 's': // Secondary DNS server IP address
        if (var[1] == '4') { opt = netIF_OptionIP4_SecondaryDNS; }
        else               { opt = netIF_OptionIP6_SecondaryDNS; }
        break;
      
      default: var[0] = '\0'; break;
    }

    switch (var[1]) {
      case '4': typ = NET_ADDR_IP4; break;
      case '6': typ = NET_ADDR_IP6; break;

      default: var[0] = '\0'; break;
    }

    if ((var[0] != '\0') && (var[2] == '=')) {
      netIP_aton (&var[3], typ, ip_addr);
      // Set required option
      netIF_SetOption (NET_IF_CLASS_ETH, opt, ip_addr, sizeof(ip_addr));
    }
  } while (qstr);
}



/*----------------------------------------------------------------------------
  2. netCGI_ProcessData: Procesa datos enviados por formularios (POST)
  Se ejecuta cuando el usuario pulsa "Send" o cambia un Checkbox en la web.
 *---------------------------------------------------------------------------*/
void netCGI_ProcessData (uint8_t code, const char *data, uint32_t len) {
  char var[40], passw[12];
  MSGQUEUE_OBJ_LCD_t msg_lcd; // Variable para mensaje de LCD
  bool update_lcd = false;		// Bandera para saber si tenemos que actualizar el LCD al final

  if (code != 0) {
    return; // Ignoramos si no es un formulario web
  }

  P2 = 0;           // Reset de estado de LEDs (se reconstruye con el formulario)
  LEDrun = true;    // Por defecto, permitimos que el hilo de BlinkLed corra

  if (len == 0) {
    LED_SetOut (P2);
    return;
  }

  passw[0] = 1;

  do {
    // Extraemos la siguiente variable del formulario
    data = netCGI_GetEnvVar (data, var, sizeof (var));
    if (var[0] != 0) {
      
      /* --- GESTIÓN DE LEDS  --- */
      if      (strcmp (var, "led0=on") == 0) P2 |= 0x01;
      else if (strcmp (var, "led1=on") == 0) P2 |= 0x02;
      else if (strcmp (var, "led2=on") == 0) P2 |= 0x04;
      else if (strcmp (var, "led3=on") == 0) P2 |= 0x08;
      else if (strcmp (var, "led4=on") == 0) P2 |= 0x10;
      else if (strcmp (var, "led5=on") == 0) P2 |= 0x20;
      else if (strcmp (var, "led6=on") == 0) P2 |= 0x40;
      else if (strcmp (var, "led7=on") == 0) P2 |= 0x80;
      else if (strcmp (var, "ctrl=Browser") == 0) LEDrun = false;

      /* --- GESTIÓN DE PASSWORD (Opcional) --- */
      else if ((strncmp (var, "pw0=", 4) == 0) || (strncmp (var, "pw2=", 4) == 0)) {
        if (netHTTPs_LoginActive()) {
          if (passw[0] == 1) strcpy (passw, var+4);
          else if (strcmp (passw, var+4) == 0) netHTTPs_SetPassword (passw);
        }
      }

      /* --- GESTIÓN DE TU LCD PERSONALIZADO --- */
      else if (strncmp (var, "lcd1=", 5) == 0) {
        // Guardamos el texto en el array global por si acaso
        strcpy (lcd_text[0], var+5); 
        update_lcd = true;
      }
      else if (strncmp (var, "lcd2=", 5) == 0) {
        // Guardamos el texto en el array global
        strcpy (lcd_text[1], var+5); 
        update_lcd = true;
      }
    }
  } while (data);

  // 1. Aplicamos cambios a los LEDs
  LED_SetOut (P2);

  // 2. Si ha llegado texto nuevo para el LCD, enviamos el mensaje a la cola IPC
  if (update_lcd) {
    // Limpiamos la estructura de mensaje
    memset(&msg_lcd, 0, sizeof(MSGQUEUE_OBJ_LCD_t));
    
    // Copiamos los textos recibidos a la estructura del mensaje
    strncpy(msg_lcd.Lin1, lcd_text[0], sizeof(msg_lcd.Lin1) - 1);
    strncpy(msg_lcd.Lin2, lcd_text[1], sizeof(msg_lcd.Lin2) - 1);
    
    // Opcional: podrías poner valores por defecto para barra/amplitud si quieres
    msg_lcd.barra = 0;
    msg_lcd.amplitud = 0;

    // ENVIAMOS A LA COLA (IPC)
    // mid_messageQueueLCD debe estar declarada como 'extern' en lcd.h
    osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0);
  }
}

/*----------------------------------------------------------------------------
  3. netCGI_Script: El "Generador" de contenido dinámico.
  Se ejecuta cuando el servidor lee una línea que empieza por 't' en un .cgi.
  Sustituye los comandos especiales por datos reales del micro.
 *---------------------------------------------------------------------------*/
uint32_t netCGI_Script (const char *env, char *buf, uint32_t buflen, uint32_t *pcgi) {
  int32_t socket;
  netTCP_State state;
  NET_ADDR r_client;
  const char *lang;
  uint32_t len = 0U;
  uint8_t id;
  static uint32_t adv;
  netIF_Option opt = netIF_OptionMAC_Address;
  int16_t      typ = 0;
	
	char t_str[20], d_str[20];

  switch (env[0]) {
    // Analyze a 'c' script line starting position 2
    case 'a' :
      // Network parameters from 'network.cgi'
      switch (env[3]) {
        case '4': typ = NET_ADDR_IP4; break;
        case '6': typ = NET_ADDR_IP6; break;

        default: return (0);
      }
      
      switch (env[2]) {
        case 'l':
          // Link-local address
          if (env[3] == '4') { return (0);                             }
          else               { opt = netIF_OptionIP6_LinkLocalAddress; }
          break;

        case 'i':
          // Write local IP address (IPv4 or IPv6)
          if (env[3] == '4') { opt = netIF_OptionIP4_Address;       }
          else               { opt = netIF_OptionIP6_StaticAddress; }
          break;

        case 'm':
          // Write local network mask
          if (env[3] == '4') { opt = netIF_OptionIP4_SubnetMask; }
          else               { return (0);                       }
          break;

        case 'g':
          // Write default gateway IP address
          if (env[3] == '4') { opt = netIF_OptionIP4_DefaultGateway; }
          else               { opt = netIF_OptionIP6_DefaultGateway; }
          break;

        case 'p':
          // Write primary DNS server IP address
          if (env[3] == '4') { opt = netIF_OptionIP4_PrimaryDNS; }
          else               { opt = netIF_OptionIP6_PrimaryDNS; }
          break;

        case 's':
          // Write secondary DNS server IP address
          if (env[3] == '4') { opt = netIF_OptionIP4_SecondaryDNS; }
          else               { opt = netIF_OptionIP6_SecondaryDNS; }
          break;
      }

      netIF_GetOption (NET_IF_CLASS_ETH, opt, ip_addr, sizeof(ip_addr));
      netIP_ntoa (typ, ip_addr, ip_string, sizeof(ip_string));
      len = (uint32_t)sprintf (buf, &env[5], ip_string);
      break;
    // --- CASO 'b': Estado de los LEDs ---
    case 'b':
      // LED control from 'led.cgi'
      if (env[2] == 'c') {
        // Select Control
        len = (uint32_t)sprintf (buf, &env[4], LEDrun ?     ""     : "selected",
                                               LEDrun ? "selected" :    ""     );
        break;
      }
      // LED CheckBoxes
      id = env[2] - '0';
      if (id > 7) {
        id = 0;
      }
      id = (uint8_t)(1U << id);
      len = (uint32_t)sprintf (buf, &env[4], (P2 & id) ? "checked" : "");
      break;

    case 'c':
      // TCP status from 'tcp.cgi'
      while ((uint32_t)(len + 150) < buflen) {
        socket = ++MYBUF(pcgi)->idx;
        state  = netTCP_GetState (socket);

        if (state == netTCP_StateINVALID) {
          /* Invalid socket, we are done */
          return ((uint32_t)len);
        }

        // 'sprintf' format string is defined here
        len += (uint32_t)sprintf (buf+len,   "<tr align=\"center\">");
        if (state <= netTCP_StateCLOSED) {
          len += (uint32_t)sprintf (buf+len, "<td>%d</td><td>%d</td><td>-</td><td>-</td>"
                                             "<td>-</td><td>-</td></tr>\r\n",
                                             socket,
                                             netTCP_StateCLOSED);
        }
        else if (state == netTCP_StateLISTEN) {
          len += (uint32_t)sprintf (buf+len, "<td>%d</td><td>%d</td><td>%d</td><td>-</td>"
                                             "<td>-</td><td>-</td></tr>\r\n",
                                             socket,
                                             netTCP_StateLISTEN,
                                             netTCP_GetLocalPort(socket));
        }
        else {
          netTCP_GetPeer (socket, &r_client, sizeof(r_client));

          netIP_ntoa (r_client.addr_type, r_client.addr, ip_string, sizeof (ip_string));
          
          len += (uint32_t)sprintf (buf+len, "<td>%d</td><td>%d</td><td>%d</td>"
                                             "<td>%d</td><td>%s</td><td>%d</td></tr>\r\n",
                                             socket, netTCP_StateLISTEN, netTCP_GetLocalPort(socket),
                                             netTCP_GetTimer(socket), ip_string, r_client.port);
        }
      }
      /* More sockets to go, set a repeat flag */
      len |= (1u << 31);
      break;

    case 'd':
      // System password from 'system.cgi'
      switch (env[2]) {
        case '1':
          len = (uint32_t)sprintf (buf, &env[4], netHTTPs_LoginActive() ? "Enabled" : "Disabled");
          break;
        case '2':
          len = (uint32_t)sprintf (buf, &env[4], netHTTPs_GetPassword());
          break;
      }
      break;

    case 'e':
      // Browser Language from 'language.cgi'
      lang = netHTTPs_GetLanguage();
      if      (strncmp (lang, "en", 2) == 0) {
        lang = "English";
      }
      else if (strncmp (lang, "de", 2) == 0) {
        lang = "German";
      }
      else if (strncmp (lang, "fr", 2) == 0) {
        lang = "French";
      }
      else if (strncmp (lang, "sl", 2) == 0) {
        lang = "Slovene";
      }
      else {
        lang = "Unknown";
      }
      len = (uint32_t)sprintf (buf, &env[2], lang, netHTTPs_GetLanguage());
      break;
		// --- CASO 'f': Mostrar texto actual del LCD en la web ---
    case 'f':
      // LCD Module control from 'lcd.cgi'
      switch (env[2]) {
        case '1':
          len = (uint32_t)sprintf (buf, &env[4], lcd_text[0]);
          break;
        case '2':
          len = (uint32_t)sprintf (buf, &env[4], lcd_text[1]);
          break;
      }
      break;
		// --- CASO 'g': Entrada del ADC (Potenciómetro) ---
    case 'g':
      // AD Input from 'ad.cgi'
      switch (env[2]) {
        case '1':
          adv = AD_in (0);
          len = (uint32_t)sprintf (buf, &env[4], adv);
          break;
        case '2':
          len = (uint32_t)sprintf (buf, &env[4], (double)((float)adv*3.3f)/4096);
          break;
        case '3':
          adv = (adv * 100) / 4096;
          len = (uint32_t)sprintf (buf, &env[4], adv);
          break;
      }
      break;
    // --- CASO 't' (NUEVO P2): Mostrar Hora y Fecha del RTC ---
    case 't':
      RTC_ObtenerHoraFecha(t_str, d_str); // Llamamos a tu biblioteca rtc.c
      if (env[1] == 'h') len = sprintf (buf, "%s", t_str); // Si pides 'th' mandamos hora
      if (env[1] == 'f') len = sprintf (buf, "%s", d_str); // Si pides 'tf' mandamos fecha
      break;
    case 'x':
      // AD Input from 'ad.cgx'
      adv = AD_in (0);
      len = (uint32_t)sprintf (buf, &env[1], adv);
      break;

    case 'y':
      // Button state from 'button.cgx'
      len = (uint32_t)sprintf (buf, "<checkbox><id>button%c</id><on>%s</on></checkbox>",
                               env[1], (get_button () & (1 << (env[1]-'0'))) ? "true" : "false");
      break;
  }
  return (len);
}

#if      defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
#pragma  clang diagnostic pop
#endif
