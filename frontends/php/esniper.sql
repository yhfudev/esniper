# phpMyAdmin SQL Dump
# version 2.5.4
# http://www.phpmyadmin.net
#
# Host: localhost
# Erstellungszeit: 22. Februar 2005 um 16:59
# Server Version: 4.0.15
# PHP-Version: 4.3.3
# 
# Datenbank: `esniper`
# 

# --------------------------------------------------------

#
# Tabellenstruktur für Tabelle `gruppen`
#

CREATE TABLE `gruppen` (
  `gruppeID` int(11) NOT NULL auto_increment,
  `name` varchar(30) NOT NULL default '',
  `notizen` text NOT NULL,
  PRIMARY KEY  (`gruppeID`),
  UNIQUE KEY `name` (`name`)
) TYPE=MyISAM AUTO_INCREMENT=13 ;

# --------------------------------------------------------

#
# Tabellenstruktur für Tabelle `snipe`
#

CREATE TABLE `snipe` (
  `artnr` bigint(10) NOT NULL default '0',
  `bid` text NOT NULL,
  `gruppe` smallint(6) NOT NULL default '0',
  `endtime` bigint(10) NOT NULL default '0',
  `highestBid` text NOT NULL,
  `pid` mediumint(6) NOT NULL default '0',
  `status` smallint(1) NOT NULL default '0',
  PRIMARY KEY  (`artnr`)
) TYPE=MyISAM;
