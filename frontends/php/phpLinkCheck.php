<?php

function phpLinkCheck($url, $r = FALSE)
{
  /*  Purpose: Check HTTP Links
   *  Usage:   $var = phpLinkCheck(absoluteURI)
   *           $var["Status-Code"] will return the HTTP status code
   *           (e.g. 200 or 404). In case of a 3xx code (redirection)
   *           $var["Location-Status-Code"] will contain the status
   *           code of the new loaction.
   *           See print_r($var) for the complete result
   *
   *  Author:  Johannes Froemter <j-f@gmx.net>
   *  Date:    2001-04-14
   *  Version: 0.1 (currently requires PHP4)
   */

  $url = trim($url);
  if (!preg_match("=://=", $url)) $url = "http://$url";
  $url = parse_url($url);
  if (strtolower($url["scheme"]) != "http") return FALSE;

  if (!isset($url["port"])) $url["port"] = 80;
  if (!isset($url["path"])) $url["path"] = "/";

  $fp = fsockopen($url["host"], $url["port"], &$errno, &$errstr, 30);

  if (!$fp) return FALSE;
  else
  {
    $head = "";
    $httpRequest = "HEAD ". $url["path"] ." HTTP/1.1\r\n"
                  ."Host: ". $url["host"] ."\r\n"
                  ."Connection: close\r\n\r\n";
    fputs($fp, $httpRequest);
    while(!feof($fp)) $head .= fgets($fp, 1024);
    fclose($fp);

    preg_match("=^(HTTP/\d+\.\d+) (\d{3}) ([^\r\n]*)=", $head, $matches);
    $http["Status-Line"] = $matches[0];
    $http["HTTP-Version"] = $matches[1];
    $http["Status-Code"] = $matches[2];
    $http["Reason-Phrase"] = $matches[3];

    if ($r) return $http["Status-Code"];

    $rclass = array("Informational", "Success",
                    "Redirection", "Client Error",
                    "Server Error");
    $http["Response-Class"] = $rclass[$http["Status-Code"][0] - 1];

    preg_match_all("=^(.+): ([^\r\n]*)=m", $head, $matches,
PREG_SET_ORDER);
    foreach($matches as $line) $http[$line[1]] = $line[2];

    if ($http["Status-Code"][0] == 3)
      $http["Location-Status-Code"] = phpLinkCheck($http["Location"],
TRUE);

    return $http;
  }
}

?>
