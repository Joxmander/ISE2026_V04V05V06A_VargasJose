t <html><head><title>Estado del RTC</title></head>
i pg_header.inc
t <h2 align=center><br>Estado del Reloj Interno (RTC)</h2>
t <p><font size="2">Esta pagina muestra la <b>Hora</b> y la <b>Fecha</b> actuales
t  almacenadas en el microcontrolador STM32.<br><br>
t  <b>Nota:</b> Refresca la pagina (F5) para actualizar los valores.</font></p>
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
t </font></table>
i pg_footer.inc
.
