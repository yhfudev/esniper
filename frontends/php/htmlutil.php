<?php
function html_Gruppenliste($artnr,$db) {
//Baut eine Auswahlliste für die Gruppen zusammen
    $sql = "SELECT gruppe FROM snipe WHERE artnr = ".$artnr;
    $snipe = $db->get_row($sql);
    $temp = "<select name=\"gruppe\" size=\"1\">";
    if ($snipe->gruppe == 0) {
	$temp .= "<option selected=\"selected\">keine</option>";
    } else {
	$temp .= "<option>keine</option>";
    }
    $sql = "SELECT * FROM gruppen";
    $namenliste = $db->get_results($sql);
    if (!empty($namenliste)) {
        foreach($namenliste as $name) {
    	    if ($snipe->gruppe == $name->gruppeID) {
                $temp .= "<option selected=\"selected\">".$name->name."</option>";
	    } else {
		$temp .= "<option>".$name->name."</option>";
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
    switch($code) {
	Case 0:
	    return("snipeing...");
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
