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
<p class="ueberschrift">neuer Artikel</p>
<?php
require 'utils.php';

$artnr = $_GET["artnr"];
$bid   = $_GET["bid"];
$gruppe = $_GET["gruppe"];

//Eintrag erstellen
if ($artnr != "" && $bid != "") {
    snipeEinstellen($artnr,$bid,$db);
}


if ($gruppe != "") {
    if ($gruppe == "keine") {
        $gruppeID = 0;
    } else {
        $gruppeID = $db->get_var("SELECT gruppeID FROM gruppen WHERE name = \"".$gruppe."\"");
    }
    $sql = "UPDATE snipe SET gruppe = ".$gruppeID." WHERE artnr = ".$artnr;
    $db->query($sql);
}


?>
<form  action="neuerArtikel.php" method="get">
<fieldset><legend><b>Artikeldaten</b></legend>
  <table cellpadding="2" cellspacing="3">
    <tr>
      <td valign="top">Auktionsnummer</td>
      <td valign="top">
        <input type="text" size="10" name="artnr">
      </td>
      <td valign="top"> Gruppe <select name="gruppe" size="1">
          <option selected="selected">keine</option>
          <?php
		$sql = "SELECT name FROM gruppen";
		$namenliste = $db->get_results($sql);
		foreach($namenliste as $name) {
		    printf("<option>".$name->name."</option>");
		}
	    ?>
        </select> </td>
    </tr>
    <tr>
      <td valign="top">Höchstgebot</td>
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