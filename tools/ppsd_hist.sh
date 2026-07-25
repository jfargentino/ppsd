#!/bin/bash

GPLOT=./tools/ppsd_hist.gplot
SHOW_PLOT=1
STATS=./pps_stats

_offset_min () {
    cat "$1" | $STATS -I | grep LOWEST | tail -n 1 | awk -F',' '{print $2}'
}

_offset_max () {
    cat "$1" | $STATS -I | grep HIGHEST | tail -n 1 | awk -F',' '{print $2}'
}

_offsets () {
    cat "$1" | $STATS | awk -F',' '{print $2}'
}

_plot_histo () {
    if [ $2 ]; then
        N=$2
    else
        N=20
    fi
    OMIN=`_offset_min "$1"`
    OMAX=`_offset_max "$1"`
    OFILE="$1"".off"
    _offsets "$1" > "$OFILE"
    gnuplot -e "OFFSET_FILE=\"$OFILE\"" -e "N=$N"\
            -e "OFFSET_MIN=$OMIN" \
            -e "OFFSET_MAX=$OMAX" -p $GPLOT
    if [ $SHOW_PLOT -gt 0 ]; then
        xdg-open "$OFILE""-hist.png" 2> /dev/null
    fi
}

for f in $*; do
    _plot_histo "$f"
done

