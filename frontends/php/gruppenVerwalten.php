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
<p class="ueberschrift">Gruppenverwaltung</p>
<?php
require 'utils.php';
require 'htmlutil.php';

$name = $_GET["name"];
$notizen   = $_GET["notizen"];
$zutun = $_GET["zutun"];
$gruppenliste = $_GET["gruppenliste"];

switch($zutun) {
    Case 1:
    if ($name != "") {
        $sql = "INSERT INTO gruppen (name,notizen) VALUES (\"$name\",\"$notizen\")";
        $db->query($sql);
        printf("Gruppe ".$name." wurde erstellt.");
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
	printf("Bitte eine Gruppe auswählen!");
    }
    break;
}


?>
<form  action="gruppenVerwalten.php" method="get">
<fieldset><legend><b>Gruppe anlegen</b></legend>
<table>
<tr>
    <td>Gruppenname</td>
    <td>Notizen</td>
</tr>
<tr>
    <td valign="top" align="left"><input type="text" size="20" name="name"><br>
	<input type="hidden" name="zutun" value=1>
	<input type="submit" value="erstellen">
    </td>
    <td>
	<textarea name="notizen" cols="50" rows="10"></textarea>
    </td>
</tr>
</table>
</fieldset>
</form>

<form action="gruppenVerwalten.php" method="get">
<fieldset><legend><b>Gruppe löschen</b></legend>
<table>
<tr>
    <td>Gruppenauswahl</td>
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
	<input type="submit" value="löschen">
    </td>
</tr>
</table>
</fieldset>
</form>

</body>
</html>