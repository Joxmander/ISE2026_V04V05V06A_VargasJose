t <html><head><title>Energia SECRM</title>
t <script language=JavaScript>
t function submit_form() {
t   document.pwr_form.submit();
t }
t </script>
t </head>
i pg_header.inc
t <h2 align=center><br>Gestion de Energia - Proyecto SECRM</h2>
t <p><font size="2">Configura el comportamiento del <b>Nodo B</b> cuando no hay actividad. 
t <br>El sistema entrara en el modo seleccionado automaticamente tras 15 segundos.</font></p>
t <br>
t <table border=0 width=99%><font size="3">
t <tr bgcolor=#aaccff>
t  <th width=40%>Estado Actual</th>
t  <th width=60%>Valor</th></tr>
t <tr><td><img src=pabb.gif>Modo configurado:</td>
t  <td><b style="color:red;">
c p0
t </b></td></tr>
t </font></table>
t <br>
t <form action="power.cgi" method="POST" name="pwr_form">
t <table border=0 width=99%><font size="3">
t <tr bgcolor=#aaccff>
t  <th width=40%>Seleccion de Modo (Apartado 3)</th>
t  <th width=60%>Opciones</th></tr> 
t <tr><td><img src=pabb.gif>Politica de ahorro:</td>
t <td>
t   <input type="radio" name="pw_mode" value="0"
c p1
t   > <b>Modo Sleep:</b> Solo CPU parada. Despertar instantaneo.<br>
t   <input type="radio" name="pw_mode" value="1"
c p2
t   > <b>Modo STOP:</b> Relojes parados. Ahorro critico (Pilas).
t </td></tr>
t <tr><td colspan="2" align="center"><br>
t   <input type="button" value="Guardar Configuracion" onclick="submit_form()">
t </td></tr>
t </font></table>
t </form>
i pg_footer.inc
.
