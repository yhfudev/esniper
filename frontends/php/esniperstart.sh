#!/bin/sh
$3 -c $4 $1 > $2 2>&1
./reload.php &