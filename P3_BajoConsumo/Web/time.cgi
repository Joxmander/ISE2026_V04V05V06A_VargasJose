t <html><head><title>Estado del RTC</title>
t <script language=JavaScript>
t function submit_form() {
t   document.rtc_form.submit();
t }
t </script>
t </head>
i pg_header.inc
t <h2 align=center><br>Estado del Reloj Interno (RTC) y SNTP</h2>
t <p><font size="2">Esta pagina muestra la <b>Hora</b> y la <b>Fecha</b> actuales
t  almacenadas en el microcontrolador STM32.<br><br>
t  <b>Nota:</b> Usa el boton "Refrescar Pagina" para actualizar la hora sin avisos molestos.</font></p>
t 
t <div align="center">
t   <input type="button" value="Refrescar Pagina" onclick="window.location.href='time.cgi';">
t </div>
t <br>
t 
t <table border=0 width=99%><font size="3">
t <tr bgcolor=#aaccff>
t  <th width=40%>Parametro</th>
t  <th width=60%>Valor Actual</th></tr>
t <tr><td><img src=pabb.gif>Hora del sistema:</td>
t <td><b>
c h1
t </b></td></tr>
t <tr><td><img src=pabb.gif>Fecha del sistema:</td>
t <td><b>
c h2
t </b></td></tr>
t <tr><td><img src=pabb.gif>Servidor SNTP en uso:</td>
t <td><b style="color:blue;">
c s4
t </b></td></tr>
t </font></table>
t <br> 
t 
t <form action="time.cgi" method="POST" name="rtc_form">
t <table border=0 width=99%><font size="3">
t <tr bgcolor=#aaccff>
t  <th width=40%>Configuracion (Apartado 5)</th>
t  <th width=60%>Opciones</th></tr> 
t 
t <tr><td><img src=pabb.gif>Cambiar Servidor SNTP:</td>
t <td>
t   <input type="radio" name="sntp" value="0"
c s10
t   > Google NTP (216.239.35.0)<br>
t   <input type="radio" name="sntp" value="1"
c s11
t   > Cloudflare NTP (162.159.200.1)
t </td></tr>
t 
t <tr><td><img src=pabb.gif>Estado de la Alarma:</td>
t <td>
t   <input type="checkbox" name="alm_en" value="on"
c s2
t   > Habilitar alarma del RTC (LED Verde)
t </td></tr>
t 
t <tr><td><img src=pabb.gif>Periodo de la Alarma:</td>
t <td>
t   <select name="periodo">
t     <option value="1"
c s31
t     >Cada 10 segundos</option>
t     <option value="2"
c s32
t     >Cada 1 minuto</option>
t     <option value="3"
c s33
t     >Cada 5 minutos</option>
t   </select>
t </td></tr>
t 
t <tr><td colspan="2" align="center">
t   <input type="button" value="Aplicar Cambios" onclick="submit_form()">
t </td></tr>
t 
t </font></table>
t </form>
i pg_footer.inc
.
