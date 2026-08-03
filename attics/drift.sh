#!/bin/bash


DRIFT_PARAMS=""
DRIFT_FILE_BASE=`date +%s`

while getopts a:p:P:ACN:O:D:sduf:F:h OPT
do
    case $OPT in
        # GPSD address and port
        a|p)
            DRIFT_PARAMS="$DRIFT_PARAMS -$OPT $OPTARG"
            ;;
        # PPS and PPS params
        P|N|O|D)
            DRIFT_PARAMS="$DRIFT_PARAMS -$OPT $OPTARG"
            ;;
        # PPS assert xor clear
        A|C)
            DRIFT_PARAMS="$DRIFT_PARAMS -$OPT"
            ;;
        # set clock, adjust drift, update file
        s|d|u)
            DRIFT_PARAMS="$DRIFT_PARAMS -$OPT"
            ;;
        # drift file to use
        f)
            DRIFT_PARAMS="$DRIFT_PARAMS -$OPT $OPTARG"
            ;;
        F)
            DRIFT_FILE_BASE="$OPTARG"
            ;;
        h)
            ./drift --help
            exit 0
            ;;
        *)
            echo "Try '$0 -h' for more information."
            exit 1
    esac
done

stdbuf -oL ./drift $DRIFT_PARAMS | tee "$DRIFT_FILE_BASE"".txt"

grep -v "^#" "$DRIFT_FILE_BASE"".txt" \
  | grep -v '^$' | tail -n +2 > "$DRIFT_FILE_BASE"".off"

N=`wc -l "$DRIFT_FILE_BASE"".off" | awk -F' ' '{print $1}'`
if [ $N -eq 0 ]; then
    echo "$DRIFT_FILE_BASE"".off is empty"
    rm "$DRIFT_FILE_BASE"".off"
else
    #octave-cli --eval "drift(\""$DRIFT_FILE_BASE"".off"\", 3);"
    #xdg-open "$DRIFT_FILE_BASE"".off.png" 2> /dev/null
    gnuplot -e "DRIFT_FILE=\"$DRIFT_FILE_BASE.off\"" -p drift.gnuplot
fi

