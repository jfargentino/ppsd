#!/bin/bash

GPLOT=./tools/ppsd_plot.gplot
SHOW_PLOT=1

_filter_data () {
    grep -v "^#" "$1" | grep -v '^$' | tail -n +2
}

_plot_offset () {
    gnuplot -e "PPS_OFFSET_FILE=\"$1\"" -p $GPLOT
    if [ $SHOW_PLOT -gt 0 ]; then
        xdg-open "$1"".png" 2> /dev/null
    fi
}

for f in $*; do
    g=`echo $f | sed 's/\..\+$//'`
    _filter_data "$f" > "$g"".off"
    N=`wc -l "$g"".off" | awk -F' ' '{print $1}'`
    if [ $N -eq 0 ]; then
        echo "$g"".off is empty"
        rm "$g"".off"
    else
        _plot_offset "$g"".off"
    fi
done

