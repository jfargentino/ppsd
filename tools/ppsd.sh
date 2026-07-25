#!/bin/bash

TIMEREF=./timeref
PPSD=./ppsd
PLOT=./tools/ppsd_plot.sh
HIST=./tools/ppsd_hist.sh

PPSD_PARAMS=""
DRIFT_FILE_BASE=`date +%s`
SET_CLOCK=0
PLOT_OFFSET=1

while getopts psP:ACqvN:O:d:c:o:m:Dh OPT
do
    case $OPT in
        p)
            PLOT_OFFSET=1
            ;;
        s)
            SET_CLOCK=1
            ;;
        d|c|m)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT $OPTARG"
            ;;
        # PPS and PPS params
        P|N|O|q|v)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT $OPTARG"
            ;;
        # PPS assert xor clear
        A|C)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT"
            ;;
        # plot results when 1 round only
        D)
            PLOT_OFFSET=1
            PPSD_PARAMS="$PPSD_PARAMS -$OPT"
            ;;
        h)
            ./ppsd --help
            exit 0
            ;;
        *)
            echo "Try '$0 -h' for more information."
            exit 1
    esac
done

if [ $SET_CLOCK -gt 0 ]; then
    $TIMEREF -s  | tee "$DRIFT_FILE_BASE"".txt"
fi

stdbuf -oL $PPSD $PPSD_PARAMS | tee -a "$DRIFT_FILE_BASE"".txt"

if [ $PLOT_OFFSET -gt 0 ]; then
    $PLOT "$DRIFT_FILE_BASE"".txt"
    $HIST "$DRIFT_FILE_BASE"".txt"
fi
