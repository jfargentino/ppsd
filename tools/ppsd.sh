#!/bin/bash

TIMEREF=./timeref
PPSD=./ppsd
PLOT=./tools/ppsd_plot.sh
HIST=./tools/ppsd_hist.sh

SET_CLOCK=0
PLOT_OFFSET=1
PPSD_PARAMS=""

DRIFT_MAX_ppb=0
OFFSET_MIN_ns=0
OFFSET_MAX_ns=0

while getopts ps1N:n:ACH:D:o:O:qvh OPT
do
    case $OPT in
        p)
            PLOT_OFFSET=0
            ;;
        s)
            SET_CLOCK=1
            ;;
        # PPS assert xor clear
        A|C)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT"
            ;;
        # PPS offset
        H)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT $OPTARG"
            ;;
        # PPS count
        N|n)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT $OPTARG"
            ;;
        # do once
        1)
            PPSD_PARAMS="$PPSD_PARAMS -$OPT"
            ;;
        # drift max, allow drift correction 
        D)
            DRIFT_MAX_ppb=$OPTARG
            ;;
        # offset min / max, allow offset correction
        o)
            OFFSET_MIN_ns=$OPTARG
            ;;
        O)
            OFFSET_MAX_ns=$OPTARG
            ;;
        h)
            ./ppsd --help
            echo "ppsd.sh [-s] ..."
            exit 0
            ;;
        *)
            echo "Try '$0 -h' for more information."
            exit 1
    esac
done

if [ $SET_CLOCK -gt 0 ]; then
    if [ $DRIFT_MAX_ppb -eq 0 ]; then
        DRIFT_MAX_ppb=1000000
    fi
    if [ $OFFSET_MIN_ns -eq 0 ]; then
        OFFSET_MIN_ns=-500000000
    fi
    if [ $OFFSET_MAX_ns -eq 0 ]; then
        OFFSET_MAX_ns=+500000000
    fi
    $TIMEREF -s  | tee "$DRIFT_FILE_BASE"".txt"
fi
if [ $DRIFT_MAX_ppb -ne 0 ]; then
    PPSD_PARAMS="$PPSD_PARAMS -D $DRIFT_MAX_ppb"
fi
if [ $OFFSET_MIN_ns -ne 0 ]; then
    PPSD_PARAMS="$PPSD_PARAMS -o $OFFSET_MIN_ns"
fi
if [ $OFFSET_MAX_ns -ne 0 ]; then
    PPSD_PARAMS="$PPSD_PARAMS -O $OFFSET_MAX_ns"
fi

PPS_FILE=`date +%s`".pps"
stdbuf -oL $PPSD $PPSD_PARAMS | tee -a $PPS_FILE

if [ $PLOT_OFFSET -gt 0 ]; then
    $PLOT $PPS_FILE
    $HIST $PPS_FILE
fi

