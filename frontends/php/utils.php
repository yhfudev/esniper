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
require 'config.inc';
require 'ez_sql.php';
require 'phpLinkCheck.php';

global $db;
$db = new db(EZSQL_DB_USER, EZSQL_DB_PASSWORD, EZSQL_DB_NAME, EZSQL_DB_HOST);

function genAuctionfile($artnr,$bid) {
    $fn=TMP_FOLDER."/".$artnr.".ebaysnipe";
    $text="$artnr $bid\n";
    $fp=fopen($fn,"w");
    fwrite($fp,$text);
    fclose($fp);
    chmod($fn, 0666);
}

function startEsniper($artnr) {
    $fn=TMP_FOLDER."/".$artnr.".ebaysnipe";
    $fnl=TMP_FOLDER."/".$artnr.".ebaysnipelog";
    touch($fnl);
    chmod($fnl, 0666);
    $pid = exec("./esniperstart.sh $fn $fnl ".PATH_TO_ESNIPER." ".PATH_TO_ESNIPERCONFIG." > /dev/null & echo \$!", $results,$status);
    return($pid);
}

function auktionBeendet($artnr) {
    $fn=TMP_FOLDER."/".$artnr.".ebaysnipelog";
    if (file_exists($fn)) {
		$fp=fopen($fn,"r");
		$text=fread($fp, filesize ($fn));
		fclose($fp);
		if (ereg("You have already won", $text)) {return(1);}
		elseif (ueberbotenStatus($text)) {return(2);}
		else {return(0);}
    }
}

function auktionEndtime($text) {
	ereg("End time: [0-9]{2}\/[0-9]{2}\/[0-9]{4} [0-2][0-9]:[0-5][0-9]:[0-5][0-9]",$text, $zeitArr);
	$zeitStr = $zeitArr[count($zeitArr)-1];
	$tag = substr($zeitStr,10,2);
	$monat = substr($zeitStr,13,2);
	$jahr = substr($zeitStr,16,4);

	$stunde = substr($zeitStr,21,2);
	$minute = substr($zeitStr,24,2);
	$sekunde = substr($zeitStr,27,2);

	$unixzeit = mktime($stunde,$minute,$sekunde,$monat,$tag,$jahr);
	return($unixzeit);
}

function ueberbotenStatus($text) {
	//True meldet, dass überboten wurde.
    $bidFound = ereg("bid: [0-9]+\.?[0-9]+",$text,$meineGebote);
    $bidMinimum = ereg("Bid price less than minimum bid price",$text);
    if (($bidFound != false && substr($meineGebote[count($meineGebote)-1],5) - getHighestBid($text) <= 0) || $bidMinimum != false) {
		return(true);
    } else {
		return(false);
    }
}

function statusPruefen($artnr,$db) {
    $status = auktionBeendet($artnr);
    if ($status != 0) {
	$sql = "UPDATE snipe SET status = ".$status." WHERE artnr=".$artnr;
	$db->query($sql);
	if ($status == 1) {
	//Andere zur Gruppe gehörende Auktionen beenden/updaten.
	    $sql = "SELECT gruppe FROM snipe WHERE artnr = ".$artnr;
	    $gruppennr = $db->get_var($sql);
	    $sql = "UPDATE snipe SET status = 3 WHERE gruppe = ".$gruppennr." AND artnr <> ".$artnr;
	    $db->query($sql);
	}
    }
}

function snipeEinstellen($artnr,$bid,$db) {
    $bid = str_replace(",",".",$bid);
    $sql = "SELECT * FROM snipe WHERE artnr=".$artnr;
    $snipe = $db->get_row($sql);
    if (empty($snipe)) {
		genAuctionfile($artnr,$bid);
        //PID auslesen und in Datenbank schreiben
        $pid = startEsniper($artnr);
        $sql = "INSERT INTO snipe (artnr,bid,pid,status) VALUES (\"$artnr\",\"$bid\",\"$pid\",0)";
        $db->query($sql);
    } else {
		//Snipe bereits in Datenbank vorhanden
		if ($bid != $snipe->bid) {
			killSniper($artnr,$db);
			genAuctionfile($artnr,$bid);
			$pid = startEsniper($artnr);
			$sql="UPDATE snipe SET bid = ".$bid.",pid = ".$pid.",status = 0 WHERE artnr = ".$snipe->artnr;
			$db->query($sql);
		} elseif (!snipeRunCheck($snipe->pid)) {
			genAuctionfile($artnr,$bid);
			$pid = startEsniper($artnr);
			$sql = "UPDATE snipe SET pid = ".$pid." WHERE artnr = ".$artnr;
			$db->query($sql);
		}
    }
    exec("./updateDB.php &");  //Nach 10 Sekunden aus den Logs die Endtime in der DB updaten - multi Thread
}

function killSniper($artnr,$db) {
    $sql = "SELECT * FROM snipe WHERE artnr=".$artnr;
    $snipe = $db->get_row($sql);

    if (snipeRunCheck($snipe->pid) == true) {
       //Sicherheitsabfrag eeinbauen, ob PID auch ein esniper Programm
//	printf("Sniperprozess mit PID ".$snipe->pid."beendet.");
	    exec("kill -15 ".getEsniperPid($snipe->pid));
    }
    exec("rm ".TMP_FOLDER."/".$artnr.".*");
}

function getPids() {
    $output = shell_exec("pidof -x esniperstart.sh");
    if ($output != "\n") {
    	$pids = split(" ",rtrim($output));
    }
    return($pids);
}


function getEsniperPid($shpid) {
//Workaround
	$output = shell_exec("pstree -p|grep ".$shpid);
	if (preg_match_all("/\([0-9]+\)/",$output,$pids,PREG_PATTERN_ORDER)) {
		return(substr($pids[0][1],1,strlen($pids[0][1])-2));
	}
}


function snipeRunCheck($pid) {
    $pids = getPids();
    if (!empty($pids)) {
    	return(in_array($pid,$pids));
    } else {
    	return(false);
    }
}


function fileList($dir) {
    $fp = opendir($dir);
    while($datei = readdir($fp)) {
        if (substr($datei,-12) == "ebaysnipelog" || substr($datei,-9) == "ebaysnipe") {
            $dateien[] = "$datei";
        }
    }
    closedir($fp);
    return($dateien);
}


function getLogData($artnr) {
	$fn=TMP_FOLDER."/".$artnr.".ebaysnipelog";
	if (file_exists($fn)) {
		$fp=fopen($fn,"r");
		$text=fread($fp, filesize ($fn));
		fclose($fp);
	} else {
		$text = false;
	}
	return($text);
}




function getHighestBid($logData) {
//Filtert das höchste Gebot aus den Logs
	preg_match_all("/Currently: [0-9]+\.?[0-9]+/",$logData,$aktGebote,PREG_PATTERN_ORDER);
    return(substr($aktGebote[0][count($aktGebote[0])-1],11));
}


function updateHighestBid($db) {
	$sql = "SELECT * FROM snipe WHERE status = 0";
	$snipelist = $db->get_results($sql);
	if (!empty($snipelist)) {
		foreach($snipelist as $snipe) {
			$logData = getLogData($snipe->artnr);
			$sql = "UPDATE snipe SET highestBid = \"".getHighestBid($logData)."\" WHERE artnr = ".$snipe->artnr;
			$db->query($sql);
		}
	}
}


function updateEndtime($db) {
	$sql = "SELECT * FROM snipe WHERE endtime <= 0";
	$snipelist = $db->get_results($sql);
	if (!empty($snipelist)) {
		foreach($snipelist as $snipe) {
			$logData = getLogData($snipe->artnr);
			$unixtime = auktionEndtime($logData);
			$sql = "UPDATE snipe SET endtime = ".$unixtime." WHERE artnr = ".$snipe->artnr;
			$db->query($sql);
		}
	}
}


function snipeGenerate($db) {
//Generiert anhand der Datenbankdaten esniper Prozesse
    $msg = "";
    $sql = "SELECT * FROM snipe WHERE status = 0";
    $snipelist = $db->get_results($sql);
    if (!empty($snipelist)) {
    	foreach($snipelist as $snipe) {
			if (!snipeRunCheck($snipe->pid)) {
			//Prozess läuft nicht
				snipeEinstellen($snipe->artnr,$snipe->bid,$db);
				$msg = $msg ."Snipe für ".$snipe->artnr." gestartet.\n";
			} else {
				$msg = $msg ."Snipe für ".$snipe->artnr." läuft bereits.\n";
			}
    	}
    }
    return($msg);
}

function collectGarbage($db) {
	//$msg = "";
    //Pids abschiessen, welche nicht laufen dürfen
    $sql = "SELECT pid FROM snipe WHERE status = 0";
    $snipePids = $db->get_col($sql);
    $pids = getPids();
    if (!empty($pids)) {
		foreach($pids as $pid) {
			if (!in_Array($pid,$snipePids)) {
				$msg = $msg ."Prozess ".$pid." wurde beendet";
				exec("kill -15 ".getEsniperPid($pid));
			}
		}
    }

	//Logs löschen, von Snipes, welche nicht in der Datenbank sind.
    $sql = "SELECT artnr FROM snipe";
    $snipeArtnr = $db->get_col($sql);
    $dateien = fileList(TMP_FOLDER);
    if (!empty($dateien)) {
	    foreach($dateien as $datei) {
			if (!in_Array(substr($datei,0,10),$snipeArtnr)) {
			    exec("rm ".TMP_FOLDER."/".$datei);
			}
	    }
    }

    if (!empty($snipeArtnr)) {
    	foreach($snipeArtnr as $artnr) {
			statusPruefen($artnr,$db);
    	}
    }

    return($msg);
}
?>