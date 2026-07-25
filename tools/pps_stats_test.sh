#!/bin/bash

STATS=./pps_stats
WIN=96
COUNT=1024
file=$1
OUT=$file"."$WIN
$STATS -v -M -s -D -w $WIN -c $COUNT $file 2> $file.dbg 1> $OUT

octave --persist --eval "cd tools; pps_stats_test('../"$OUT"', $WIN)"
