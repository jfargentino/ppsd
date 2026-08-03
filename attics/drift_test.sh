#!/bin/bash

. drift.conf

DATA_DIR=`date +%Y%m%d-%H%M%S`
DRIFT_FILE="$DATA_DIR""/drift.txt"
SET_FILE="$DATA_DIR"/drift_set
SET_FILE_TXT="$SET_FILE".txt

mkdir "$DATA_DIR"

### 1st call: evaluates drift during 5mn to correct it (-i option)
FIRST_DATA_FILE="$DATA_DIR""/set_"`date +%s`
#./drift.sh -a $GPSD_ADDR -p $GPSD_PORT \
#           -P $PPS_DEV $PPS_EDGE -O $PPS_OFFSET_ns -D $PPS_DELAY_ns \
#           -N 300 -s -d -u -f $DRIFT_FILE -F "$SET_FILE"

./drift -a $GPSD_ADDR -p $GPSD_PORT \
        -P $PPS_DEV $PPS_EDGE -O $PPS_OFFSET_ns -D $PPS_DELAY_ns \
        -N 0 -s -d -u -f $DRIFT_FILE >> "$SET_FILE_TXT"

while [ 1 ]; do
    DATA_FILE="$DATA_DIR""/"`date +%s`
    ./drift.sh -a $GPSD_ADDR -p $GPSD_PORT \
               -P $PPS_DEV $PPS_EDGE -O $PPS_OFFSET_ns -D $PPS_DELAY_ns \
               -N $PPS_BEETWEEN_SET -F "$DATA_FILE"
    ./drift -a $GPSD_ADDR -p $GPSD_PORT \
            -P $PPS_DEV $PPS_EDGE -O $PPS_OFFSET_ns -D $PPS_DELAY_ns \
            -N 0 -s -d -u -f $DRIFT_FILE >> "$SET_FILE_TXT"
done

