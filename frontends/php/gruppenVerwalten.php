<html>
<head>
<title>
Ebay Snipe Webinterface
</title>
<script type="text/javascript">
<!--
	window.resizeTo(650,610);
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

$name = $_GET["name"];
$notizen   = $_GET["notizen"];
$zutun = $_GET["zutun"];
$gruppenliste = $_GET["gruppenliste"];

printf("<p class=\"ueberschrift\">".$GLOBALS["ueGruppenverwaltung"]."</p>");


switch($zutun) {
    Case 1:
    if ($name != "") {
        $sql = "INSERT INTO gruppen (name,notizen) VALUES (\"$name\",\"$notizen\")";
        $db->query($sql);
        printf($name." ".$GLOBALS["tGruppeErstellt"]);
    }
    break;
    Case 2:
    if ($gruppenliste != "") {
	$sql = "SELECT gruppeID FROM gruppen WHERE name = \"".$gruppenliste."\"";
	$gruppe = $db->get_var($sql);
	$sql = "UPDATE snipe SET gruppe = 0 WHERE gruppe = ".$gruppe;
	$db->query($sql);
	$sql = "DELETE FROM gruppen WHERE name = \"".$gruppenliste."\"";
	$db->query($sql);
    } else {
	printf($GLOBALS["tGruppeWaehlen"]);
    }
    break;
}


?>
<form  action="gruppenVerwalten.php" method="get">
<fieldset><legend><b><?php printf($GLOBALS["ueGruppeAnlegen"]); ?></b></legend>
<table>
<tr>
    <td><?php printf($GLOBALS["ueGruppenName"]); ?></td>
    <td><?php printf($GLOBALS["ueGruppenNotizen"]); ?></td>
</tr>
<tr>
    <td valign="top" align="left"><input type="text" size="20" name="name"><br>
	<input type="hidden" name="zutun" value=1>
	<input type="submit" value="<?php printf($GLOBALS["bGruppeErstellen"]); ?>">
    </td>
    <td>
	<textarea name="notizen" cols="50" rows="10"></textarea>
    </td>
</tr>
</table>
</fieldset>
</form>

<form action="gruppenVerwalten.php" method="get">
<fieldset><legend><b><?php printf($GLOBALS["ueGruppeLoeschen"]); ?></b></legend>
<table>
<tr>
    <td><?php printf($GLOBALS["tGruppnAuswahl"]); ?></td>
</tr>
<tr>
    <td valign="top" align="left">
	<?php
		$sql = "SELECT name FROM gruppen";
		$namenliste = $db->get_results($sql);
		$temp = "<select name=\"gruppenliste\">";
		if (!empty($namenliste)) {
	    	    foreach($namenliste as $name) {
	    		$temp .= "<option>".$name->name."</option>";
		    }
		}
	        $temp .= "</select>";
		printf($temp);
	?>
    </td>
</tr>
<tr>
    <td>
	<input type="hidden" name="zutun" value=2>
	<input type="submit" value="<?php printf($GLOBALS["bGruppeLoeschen"]); ?>">
    </td>
</tr>
</table>
</fieldset>
</form>

</body>
</html>