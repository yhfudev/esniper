#!/usr/bin/php -q
<?php
require 'utils.php';
updateEndtime($db);
updateHighestBid($db);
collectGarbage($db);
printf(snipeGenerate($db));
?>