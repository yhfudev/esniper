<html>
<head>
<title>
Ebay Snipe Webinterface
</title>
<script type="text/javascript">
<!--
	window.resizeTo(450,355);
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

$artnr = $_GET["artnr"];
$bid   = $_GET["bid"];
$gruppe = $_GET["gruppe"];

printf("<p class=\"ueberschrift\">".$GLOBALS["ueNeuerArtikel"]."</p>");

//Eintrag erstellen
if ($artnr != "" && $bid != "") {
    snipeEinstellen($artnr,$bid,$db);
}


if ($gruppe != "") {
    $sql = "UPDATE snipe SET gruppe = ".$gruppe." WHERE artnr = ".$artnr;
    $db->query($sql);
}


?>
<form  action="neuerArtikel.php" method="get">
<fieldset><legend><b><?php printf($GLOBALS["tArtikelDaten"]); ?></b></legend>
  <table cellpadding="2" cellspacing="3">
    <tr>
      <td valign="top"><?php printf($GLOBALS["tAuktionsNr"]); ?></td>
      <td valign="top">
        <input type="text" size="10" name="artnr">
      </td>
      <td valign="top"> Gruppe <?php printf(html_GruppenlisteNeuerArt($db)); ?>
        </td>
    </tr>
    <tr>
      <td valign="top"><?php printf($GLOBALS["tHoechstgebot"]); ?></td>
      <td valign="top">
<input type="text" size="3" name="bid"></td>
      <td valign="top">&nbsp;</td>
    </tr>
    <tr>
      <td valign="top"><input name="submit" type="submit" value="Snipe"></td>
      <td valign="top">&nbsp; </td>
      <td valign="top">&nbsp;</td>
    </tr>
  </table>
  </fieldset>
</form>

</body>
</html>