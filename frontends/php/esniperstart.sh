#!/bin/sh
$3esniper -c $4 $1 > $2 2>&1
./reload.php &