#!/bin/bash

NTP_SERVER=ntp.unice.fr
if [[ ! -z $1 ]]; then
    NTP_SERVER=$1
fi

PREV_OFFSET_s=0

while [ 1 ]; do
    X=`ntpdate -q $NTP_SERVER | tail -n 1`
    #NTP_DATE=`echo $X | awk -F' ' '{print $1 $2}'`
    #NTP_OFFSET=`echo $X | awk -F' ' '{print $4}'`
    NTP_DATE=`echo $X | awk -F' ' '{print $1 $2 $3}'`
    NTP_OFFSET=`echo $X | awk -F' ' '{print $10}'`
    NTP_OFFSET_s=`echo $NTP_OFFSET | awk -F'.' '{print $1}'`
    if [[ $PREV_OFFSET_s -ne $NTP_OFFSET_s ]]; then
        echo "#NTP Offset $NTP_OFFSET@$NTP_DATE"
        PREV_OFFSET_s=$NTP_OFFSET_s
    fi
done

