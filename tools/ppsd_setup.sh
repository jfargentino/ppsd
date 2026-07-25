#!/bin/bash

. ppsd.conf

### Stopping _ALL_ known sync daemons
systemctl stop chrony
systemctl stop ntpd
systemctl stop openntpd
systemctl stop systemd-timesyncd
systemctl stop systemd-timedated

### Stopping gpsd daemons
systemctl stop gpsd.socket
systemctl stop gpsd.service
killall -9 gpsd

if [ ! -c $PPS_DEV ]; then
    echo "$PPS_DEV does not exist yet"
    if [ $GPS_DEV != /dev/ttyTHS1 ]; then
        ldattach PPS $GPS_DEV
    else
        # JETSON
        modprobe pps-gpio
    fi
fi
if [ $GPS_DEV != /dev/ttyTHS1 ]; then
    setserial $GPS_DEV low_latency
fi

### Starting gpsd daemon with proper config
#systemctl start gpsd.service
gpsd -b -n -S $GPSD_PORT $GPS_DEV

### Resetting adjtimex params
#adjtimex -f 0 -o 0

