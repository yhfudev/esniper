<html>
<head>
<title>
Ebay Snipe Webinterface
</title>
<meta http-equiv="Pragma" content="no-cache" />
<meta http-equiv="REFRESH" content="300;URL=index.php" />
<link href="main.css" rel="stylesheet" type="text/css">
<script src="jsutil.js" type="text/javascript" language="JavaScript"></script>
<script language="JavaScript" type="text/javascript">
function layouten(einLog) {
    einLog.childNodes[0].style.top = -1 *  einLog.childNodes[0].offsetHeight + einLog.offsetHeight;
}

function zeige(logID,alles) {
//Dient zum auf und zuklappen der Logs
    if (alles) {
	document.getElementById('in'+logID).style.top = 0;
	document.getElementById('log'+logID).style.height = document.getElementById('in'+logID).style.height;
    } else {
	document.getElementById('log'+logID).style.height = 250;
        document.getElementById('log'+logID).childNodes[0].style.top = -1 *  document.getElementById('log'+logID).childNodes[0].offsetHeight + document.getElementById('log'+logID).offsetHeight;
    }
}
function erstesAufbauen() {
    dieLogs = document.getElementsByName('logs');
    for(i=0;i<=dieLogs.length;i++) {
	layouten(dieLogs[0]);
    }
}

//globale Variable für die Countdowntimer
var artliste = new Array();

function initCounter() {
//Countdowntimer werden initalisiert

getTime(artliste);
newtime = window.setTimeout("initCounter();", 1000);
}
</script>

</head>
<body style="font-family:Helvetica,Helv;" onLoad="erstesAufbauen();initCounter();">
<p class="ueberschrift"><img src="ebay_logo.gif" align="middle">Ebay Snipe Webinterface</p>
<IMG SRC="menue.gif" BORDER=0 USEMAP="#menue_Map">
<MAP NAME="menue_Map">
<AREA SHAPE="rect" ALT="Reload" COORDS="267,0,340,45" HREF="index.php" TARGET="_self">
<AREA SHAPE="rect" ALT="Gruppen verwalten" COORDS="137,0,267,45" HREF="gruppenVerwalten.php" TARGET="gruppen">
<AREA SHAPE="rect" ALT="neuen Artikel eingeben" COORDS="0,0,136,45" HREF="neuerArtikel.php" TARGET="artikel">
<AREA SHAPE="rect" ALT="gwonnene und verlorene Auktionen löschen" COORDS="360,0,425,45" HREF="index.php?zutun=3" TARGET="_self">
</MAP>
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

$zutun = $_GET["zutun"];
$artnr = $_GET["artnr"];
$bid   = $_GET["bid"];
$delete = $_GET["delete"];
$gruppe = $_GET["gruppe"];

//Eintrag erstellen

switch($zutun) {
    Case 1:
	//Artikel löschen
        killSniper($delete,$db);
        $sql = "DELETE FROM snipe WHERE artnr=".$delete;
        $snipe = $db->get_row($sql);
        break;
    Case 2:
	if ($gruppe == "keine") {
	    $gruppeID = 0;
	} else {
	    $gruppeID = $db->get_var("SELECT gruppeID FROM gruppen WHERE name = \"".$gruppe."\"");
	}
        $sql = "UPDATE snipe SET gruppe = ".$gruppeID." WHERE artnr = ".$artnr;
	$db->query($sql);
        break;
    Case 3:
    //Aufräumen
    $sql = "DELETE FROM snipe WHERE status != 0";
    $db->query($sql);
}

$sql = "SELECT count(*) FROM snipe";
$db->get_var($sql);

printf("<strong>");
//Auktion am laufen
$sql = "SELECT count(*) FROM snipe WHERE status = 0";
$anzahl = $db->get_var($sql);
printf("laufend: ". $anzahl ." ");

//Auktion gewonnen
$sql = "SELECT count(*) FROM snipe WHERE status = 1";
$anzahl = $db->get_var($sql);
printf("gewonnen: ". $anzahl ." ");

//Auktion überboten
$sql = "SELECT count(*) FROM snipe WHERE status = 2";
$anzahl = $db->get_var($sql);
printf("verloren: ". $anzahl);
printf("</strong>");
?>
<table class="Inhaltstabelle">
<?php
function read_tree ($dir) {
    global $dateien;
    $fp = opendir($dir);
    while($datei = readdir($fp)) {
	if (substr($datei,-12) == "ebaysnipelog") {
    	    $dateien[] = "$datei";
	}
    }
    closedir($fp);
}


$sql = "SELECT * FROM snipe ORDER BY status,endtime ASC";
$snipelist = $db->get_results($sql);
if (!empty($snipelist)) {
    printf ("<tr><td class=\"Inhaltstabzelle\">Artikelnummer </td>");
    printf ("<td class=\"Inhaltstabzelle\">Snipe-Status</td>");
    printf ("<td class=\"Inhaltstabzelle\">Bild</td>");
    printf ("<td class=\"Inhaltstabzelle\">Prozessstatus</td>");
    $zaehler = 0;
    foreach($snipelist as $snipe) {
		$artnr = $snipe->artnr;
		statusPruefen($artnr,$db);
		$fn="/tmp/".$artnr.".ebaysnipelog";
		if (file_exists($fn)) {
				$fp=fopen($fn,"r");
			$text=fread($fp, filesize ($fn));
			fclose($fp);
			$text = str_replace("\n","<br>",$text);  //in Textarea nicht benötigt, nimmt auch \n
		} else {
			$text = "<span style=\"color:#FF0000;font-weight:bold;\">Fehler - keine Datei zum Datenbankeintrag gefunden!</span>";
		}

		printf("<tr><td class=\"Inhaltstabzelle\"><form action=\"index.php\" method=\"get\">Artikel: <a href=\"http://cgi.ebay.de/ws/eBayISAPI.dll?ViewItem&item=".$artnr."&rd=1\">".$artnr."</a><br>Status: ".html_snipestatus($snipe->status)."<br>Gruppe: ".html_gruppenname($snipe->gruppe,$db)."<br>".html_gruppenliste($snipe->artnr,$db)."<br>");
		printf("<input type=\"hidden\" name=\"zutun\" value=2><input type=\"hidden\" name=\"artnr\" value=\"". $artnr  ."\"><input type=\"submit\" value=\"Gruppe zuordnen\"></form>");
		printf("<form action=\"index.php\" method=\"get\"><input type=\"hidden\" name=\"zutun\" value=1><input type=\"hidden\" name=\"delete\" value=".$artnr."><input type=\"submit\" value=\"löschen\"</form>");

		if ($snipe->endtime != 0) {
		//Wenn noch am snipen, Timer anzeigen.
			printf(html_countdown($snipe->artnr,$zaehler,$snipe->endtime));
			$zaehler++;
		}

		printf("Mein Gebot: <br>".$snipe->bid."<br>");
		printf("Höchstgebot: <br>".$snipe->highestBid);
		printf("</td>");
		printf ("<td class=\"Inhaltstabzelle\"><div id=\"log".$snipe->artnr."\" name=\"logs\"  style=\"position:relative;margin-left:7;margin-top:0;width:550;height:250;overflow:hidden;color:Black\"><div id=\"in".$snipe->artnr."\" style=\"position:relative;left:0;top:0\">");
		printf($text);
		printf("</div><img src=\"pfeil.gif\" style=\"position:absolute;top:0;left:535;\" onDblClick=\"zeige(".$snipe->artnr.",true);\" onClick=\"zeige(".$snipe->artnr.",false);\"></div>");
		printf("</td>");
		$url = "http://thumbs.ebaystatic.com/pict/".$artnr."8080_0.jpg";
		/*$check = phpLinkCheck($url); //Wegen erformance deaktiviert <- bringt dank Ebayproxy auch nix.
		if ($check["Status-Code"]==200) {*/
			printf ("<td class=\"Inhaltstabzelle\"><img src=\"".$url."\"></td>");
		/*} else {
			printf ("<td class=\"Inhaltstabzelle\">Kein Bild ".$check["Status-Code"]."</td>");
		}*/
		if (snipeRunCheck($snipe->pid)) {
			printf ("<td class=\"Inhaltstabzelle\">Running...<br>PID: ".$snipe->pid."</td></tr>");
		} else {
			printf ("<td class=\"Inhaltstabzelle\"><span style=\"color:#FF0000;font-weight:bold;\">Kein Prozess!!!<br>PID: ".$snipe->pid."</span></td></tr>");
		}

    }
} else {
    printf ("<b>Keine Einträge in der Datenbank</b>");
}
?>
</table>
</body>
</html>