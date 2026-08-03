#!/bin/bash

for f in $*; do
    echo "$f"
    gnuplot -e "DRIFT_FILE=\"$f\"" -p drift.gnuplot
done

