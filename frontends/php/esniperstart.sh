#!/bin/sh
$3esniper -c /opt/esniper/config $1 > $2 2>&1
./reload.php &