<html>
<head>
<title>
Ebay Snipe Webinterface
</title>
<script type="text/javascript">
<!--
	window.resizeTo(710,570);
//-->
</script>
<meta http-equiv="Pragma" content="no-cache" />
<link href="main.css" rel="stylesheet" type="text/css">
</head>
<body style="font-family:Helvetica,Helv;">
<?php
/*
 * Copyright (c) 2005 Nils Rottgardt <nils@rottgardt.org>
 * All rights reserved
 *
 * Published under BSD-licence
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
require 'utils.php';
require 'htmlutil.php';
require 'language.php';

$notizen   = $_GET["notizen"];
$zutun = $_GET["zutun"];
$gruppeID = $_GET["gruppe"];

printf("<p class=\"ueberschrift\">".$GLOBALS["ueGruppennotizen"]."</p>");

switch($zutun) {
    Case 1:
        $sql = "UPDATE gruppen SET notizen = \"".$notizen."\" WHERE gruppeID = ".$gruppeID;
        $db->query($sql);
        printf($GLOBALS["tNotizGespeichert"]);
    break;
}

if (!empty($gruppeID)) {
	$sql="SELECT notizen FROM gruppen WHERE gruppeID = ".$gruppeID;
	$dbNotizen = $db->get_var($sql);
} else {
	printf($GLOBALS["tKeineAuswahl"]);
}


printf("<fieldset>");
  printf("<legend><b>".$GLOBALS["ueGruppeAnzeigen"]."</b></legend>");
?>
<form action="gruppenNotizen.php" method="get">
<table>
<tr>
    <td><?php printf($GLOBALS["tGruppnAuswahl"]); ?></td>
</tr>
<tr>
    <td valign="top" align="left">
	<?php
		printf(html_gruppenListeNormal($gruppeID,$db));
		?>
    </td>
</tr>
<tr>
    <td>
        <input name="anzeigen" type="submit" value="<?php printf($GLOBALS["bAnzeigen"]); ?>">
    </td>
</tr>
</table>
</form>
  <p>
    <form action="gruppenNotizen.php" method="get">
	    <textarea name="notizen" cols="80" rows="10"><?php printf($dbNotizen);?></textarea><br>
		<input type="hidden" name="gruppe" value="<?php printf($gruppeID); ?>">
		<input type="hidden" name="zutun" value=1>
		<input name="speichern" type="submit" value="<?php printf($GLOBALS["bSpeichern"]); ?>">
	</form>
  </p>
  </fieldset>


</body>
</html>