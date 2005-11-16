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

switch(lang) {
	case 1:
	//Index Liste
	$GLOBALS["tSnipeStatusArray"] = array('sniping...','Auktion gewonnen','überboten','Gruppe hat gewonnen','keine Ausgabe zugeordnet');
	$GLOBALS["tSnipeListSummaryArray"] = array('laufend','gewonnen','verloren');
	$GLOBALS["tKeine"] = 'keine';
	$GLOBALS["tAlles"] = 'Alles';
	$GLOBALS["tMenueAltArray"] = array('Reload','Gruppen verwalten','neuen Artikel eingeben','gewonnene und verlorene Artikel löschen');
	$GLOBALS["tArtikelNr"] = 'Artikelnummer';
	$GLOBALS["tSnipeStatus"] = 'Snipestatus';
	$GLOBALS["tBildTableTopic"] = 'Bild';
	$GLOBALS["tProzessStatus"] = 'Prozessstatus';
	$GLOBALS["tDbLeer"] = 'keine Einträge in der Datenbank';
	$GLOBALS["tNoProcess"] = 'Kein Prozess!!!';
	//Gruppennotizen
	$GLOBALS["ueGruppennotizen"] = 'Gruppennotizen';
	$GLOBALS["tNotizGespeichert"] = 'Notiz wurde gespeichert.';
	$GLOBALS["tKeineAuswahl"] = 'keine Auswahl';
	$GLOBALS["ueGruppeAnzeigen"] = 'Gruppe Anzeigen';
	$GLOBALS["tGruppnAuswahl"] = 'Gruppenauswahl';
	$GLOBALS["bAnzeigen"] = 'Anzeigen';
	$GLOBALS["bSpeichern"] = 'Speichern';
	//Gruppe verwalten
	$GLOBALS["ueGruppenverwaltung"] = 'Gruppenverwaltung';
	$GLOBALS["tGruppeErstellt"] = 'wurde erstellt';
	$GLOBALS["tGruppeWaehlen"] = 'Bitte eine Gruppe wählen';
	$GLOBALS["ueGruppeAnlegen"] = 'Gruppe anlegen';
	$GLOBALS["ueGruppenName"] = 'Gruppenname';
	$GLOBALS["ueGruppenNotizen"] = 'Notizen';
	$GLOBALS["bGruppeErstellen"] = 'erstellen';
	$GLOBALS["ueGruppeLoeschen"] = 'Gruppe löschen';
	$GLOBALS["bGruppeLoeschen"] = 'löschen';
	
	//neuer Artikel
	$GLOBALS["ueNeuerArtikel"] = 'neuer Artikel';
	$GLOBALS["tArtikelDaten"] = 'Artikeldaten';
	$GLOBALS["tAuktionsNr"] = 'Auktionsnnummer';
	$GLOBALS["tHoechstgebot"] = 'Höchstgebot';
	
	$GLOBALS["pMenue"] = "menue-d.gif";
	
	case 2:
	//Index List
	$GLOBALS["tSnipeStatusArray"] = array('sniping...','auction won','overbidden','Group won','no message assigned');
	$GLOBALS["tSnipeListSummaryArray"] = array('running','won','lost');
	$GLOBALS["tKeine"] = 'none';
	$GLOBALS["tAlles"] = 'all';
	$GLOBALS["tMenueAltArray"] = array('reload','manage groups','add new auction','delete won and lost auctions');
	$GLOBALS["tArtikelNr"] = 'auctionnumer';
	$GLOBALS["tSnipeStatus"] = 'snipestatus';
	$GLOBALS["tBildTableTopic"] = 'image';
	$GLOBALS["tProzessStatus"] = 'processstatus';
	$GLOBALS["tDbLeer"] = 'no entries in database';
	$GLOBALS["tNoProcess"] = 'no process!!!';
	//Groupnotes
	$GLOBALS["ueGruppennotizen"] = 'groupnotes';
	$GLOBALS["tNotizGespeichert"] = 'Note saved.';
	$GLOBALS["tKeineAuswahl"] = 'no selection';
	$GLOBALS["ueGruppeAnzeigen"] = 'show group';
	$GLOBALS["tGruppnAuswahl"] = 'Groupselection';
	$GLOBALS["bAnzeigen"] = 'show';
	$GLOBALS["bSpeichern"] = 'save';
	//manage groups
	$GLOBALS["ueGruppenverwaltung"] = 'Groupmanagement';
	$GLOBALS["tGruppeErstellt"] = 'was created';
	$GLOBALS["tGruppeWaehlen"] = 'please select a group';
	$GLOBALS["ueGruppeAnlegen"] = 'create group';
	$GLOBALS["ueGruppenName"] = 'groupname';
	$GLOBALS["ueGruppenNotizen"] = 'notes';
	$GLOBALS["bGruppeErstellen"] = 'create';
	$GLOBALS["ueGruppeLoeschen"] = 'delete group';
	$GLOBALS["bGruppeLoeschen"] = 'delete';
	
	//new auction
	$GLOBALS["ueNeuerArtikel"] = 'new auction';
	$GLOBALS["tArtikelDaten"] = 'auction informations';
	$GLOBALS["tAuktionsNr"] = 'auctionnumer';
	$GLOBALS["tHoechstgebot"] = 'highest bid';
	
	$GLOBALS["pMenue"] = "menue-e.gif";
}
?>
