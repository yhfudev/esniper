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


function html_Gruppenliste($artnr,$db) {
//Baut eine Auswahlliste für die Gruppen auf einem Artikel zusammen
    $sql = "SELECT gruppe FROM snipe WHERE artnr = ".$artnr;
    $snipe = $db->get_row($sql);
    $temp = "<select name=\"gruppe\" size=\"1\">";
    if ($snipe->gruppe == 0) {
		$temp .= "<option value=\"0\" selected=\"selected\">keine</option>";
    } else {
		$temp .= "<option value=\"0\">keine</option>";
    }
    $sql = "SELECT * FROM gruppen";
    $gruppenliste = $db->get_results($sql);
    if (!empty($gruppenliste)) {
        foreach($gruppenliste as $gruppe) {
    	    if ($snipe->gruppe == $gruppe->gruppeID) {
                $temp .= "<option value=\"".$gruppe->gruppeID."\" selected=\"selected\">".$gruppe->name."</option>";
		    } else {
				$temp .= "<option value=\"".$gruppe->gruppeID."\">".$gruppe->name."</option>";
		    }
		}
    }
    $temp .= "</select>";
    return($temp);
}


function html_GruppenFilternListe($gruppeID,$db) {
//Baut eine Auswahlliste für die Gruppen zusammen
    $temp = "<select name=\"filtergruppe\" size=\"1\">";
    if ($gruppeID == -1 || empty($gruppeID)) {
    	$temp .= "<option value=\"-1\" selected=\"selected\">Alles</option>";
    } else {
    	$temp .= "<option value=\"-1\">Alles</option>";
    }

    if ($gruppeID == 0) {
    	$temp .= "<option value=\"0\" selected=\"selected\">keine</option>";
    } else {
    	$temp .= "<option value=\"0\">keine</option>";
    }
    $sql = "SELECT * FROM gruppen";
    $gruppenliste = $db->get_results($sql);
    if (!empty($gruppenliste)) {
        foreach($gruppenliste as $gruppe) {
        	if ($gruppeID == $gruppe->gruppeID) {
				$temp .= "<option value=\"".$gruppe->gruppeID."\" selected=\"selected\">".$gruppe->name."</option>";
			} else {
				$temp .= "<option value=\"".$gruppe->gruppeID."\">".$gruppe->name."</option>";
			}
		}
    }
    $temp .= "</select>";
    return($temp);
}


function html_gruppenname($gruppeID,$db) {
    $sql = "SELECT name FROM gruppen WHERE gruppeID = ".$gruppeID;
    $gruppenname = $db->get_var($sql);
    return($gruppenname);
}


function html_snipestatus($code) {
//Wandelt den Intergerwert aus der Datenbank (status) in HTML um.
    switch($code) {
	Case 0:
	    return("sniping...");
	    break;
	Case 1:
	    return("Auktion gewonnen");
	    break;
	Case 2:
	    return("<span style=\"color:#FF0000;\">überboten!!!</span>");
	    break;
	Case 3:
	    return("Gruppe hat gewonnen");
	    break;
    default:
		return("keine Ausgabe zugeordnet");
		break;
    }

}

function html_countdown($artnr,$zaehler, $datestr) {
//Fügt für einen Artikel die HTML und JavaScript Daten für den Counter ein.
	$temp = "<script language=\"JavaScript\" type=\"text/javascript\">";
	$temp .= "artliste[".$zaehler."] = new Array();";
	$temp .= "artliste[".$zaehler."][0] =".$artnr.";";
	$temp .= "artliste[".$zaehler."][1] =". date("j",$datestr) .";";
	$temp .= "artliste[".$zaehler."][2] =". date("m",$datestr) .";";
	$temp .= "artliste[".$zaehler."][3] =". date("Y",$datestr) .";";
	$temp .= "artliste[".$zaehler."][4] =". date("H",$datestr) .";";
	$temp .= "artliste[".$zaehler."][5] =". date("i",$datestr) .";";
	$temp .= "artliste[".$zaehler."][6] =". date("s",$datestr) .";";
	$temp .= "</script>";
	$temp .= "<table><tr><td bgcolor=\"black\" valign=\"bottom\">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=x".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=a".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=b".$artnr.">";
	$temp .= "<img height=21 src=\"Cc.gif\" width=9 id=c".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=y".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=z".$artnr.">";
	$temp .= "<img height=21 src=\"Cc.gif\" width=9 id=cz".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=d".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=e".$artnr.">";
	$temp .= "<img height=21 src=\"Cc.gif\" width=9 id=f".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=g".$artnr.">";
	$temp .= "<img height=21 src=\"0c.gif\" width=16 id=h".$artnr.">";
	$temp .= "</td></tr></table>";
	return($temp);
}
?>
