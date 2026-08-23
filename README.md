# PPSD

ppsd is a PPS Daemon: do statistics on a PPS device to adjust for clock drift and offset.

---

## Needs

A propperly configured PPS device and "/usr/include/sys/timepps.h".<br>
It looks like `apt install pps-tools` is necessary step to install "timepps.h".

GPSD running for "timeref" and its dev package installed (`apt install libgps-dev`).

`git clone https://github.com/jfargentino/ppsd.git`<br>
Build with "make" and any decent C compiler.

Optional: "ntpdate" to check for +/-1s off (`apt install ntpsec-ntpdate`).

---

## Run

All applications provide a quick "-h" help.

***One master only***, If setting clock, stop NTP, chrony, timesyncd and the like.

**TODO** timedatectl interference ?<br>
**TODO** systemd start scripts.<br>
**TODO** not really good when high std dev (100us when using USB adapter), looks
like drift is really high when offset calculated over 8s...<br>
**TODO** tests scripts... measuring chrony perf when usin GPS+PPS only to compare...


### timeref

1st we need to roughly set the time to be within -/+500ms. 

Use `timeref -s` to set the system time from GPSD thanks to NMEA sentence.

Something like `ntpdate` could be used instead, with probably a better precision...


### ppsd

By design, `ppsd` can handle offset between -500ms and +500ms __**ONLY**__ !

`ppsd -N 64 -n 8` Evaluates drift every 64 PPS and offset every 8 PPS, no
correction done.

`ppsd -D 1000000 -o -500000000 -O +500000000` to adjust for both drift and offset.

   - `-D 1000000` adjust for measured drift if less than 1000000ppb
   - `-o -500000000 -O +500000000` offset correction by temporary frequency change

Setting maximum drift to 0 avoid drift correction (the default behaviour), any
other value is a security in case something goes wrong so evaluated drift is
far too big to be honest... My guess use something like twice the clock drift
should be sufficient.

Offset correction can be done by abrutly setting the clock or temporary changing
its frequency. For this, 2 thresholds are used: `-o` for the min and `-O` the max.
Then when measured offset is between the min and the max, `ppsd` temporary
adjust the clock frequency to compensate its offset. If you want to always
abruptly set the clock, use the same value for the min and the max. If min > max
then no offset compensation, this is the default behaviour.
Using 2 thresholds enable to avoid any jump in the past while jump in the future
still doable to quickly compensate for very big negative offset.


**TODO** `ppsd` need root even without adjusting the clock, probably because
of PPS opening/setting, chowning "/dev/pps0" do "dialout" group do nothing...


### jfadjtimex

`jfadjtimex` is "adjtimex (2)" terminal interface using ppb and ns for units.

`jfadjtimex -f 20000` to adjust the clock frequency by 20000ppb (20ppm).

`jfadjtimex -f 0 -t 10000` to reset the clock frequency and tick to 10ms.

JF from `jfadjtimex` is for "Just the Function..." and thus avoid any name
conflict with the well known "adjtimex (8)" application.


### tools

`ppsd_plot.sh`, `ppsd_hist.sh` and a couple of OCTAVE/MATLAB scripts.

**TODO** update ppsd.sh !


---

## How to on RPI

Adding `nohz=off` to "/boot/firmware/cmdline.txt" make no arm... on my RPI5,
std dev goes from 700ns down to 300ns !

**TODO** dtoverlay=disable-bt<br>
**TODO** measuring temperature (`vcgencmd measure_temp`)<br>
**TODO** running on 1 CPU (IRQ and app ?) to avoid ISR cache flush ?<br>


### PPS

Setting PPS input (PIN7 GPIO4):
add `dtoverlay=pps-gpio,gpiopin=4` in "/boot/firmware/config.txt"

Add `pps-gpio` to "/etc/modules" ?

Need `apt install pps-tools` to have "timepps.h".

On both JETSON and RPI, PPS detection works on assert only, so we must check
for the propper PPS edge (depends on the PPS module + any MAX3232 in beetween)
and use `ppsd -H` option accordingly (**NOT TESTED**).

On a PC, `ldattach PPS /dev/ttyXTZ` may be necessary to create the PPS device.

**TODO** `setserial /dev/ttyS0 low_latency` ?


### GPS

Setting UART (PIN8 GPIO14 and PIN10 GPIO15):
add `enable_uart=1` in "/boot/firmware/config.txt"

Then run raspi-config to disable login on UART, UART device is ttyAMA0.

`apt install gpsd gpsd-clients gpsd-tools libgps-dev`

Edit "/etc/default/gpsd" to add `DEVICES="/dev/ttyAMA0 /dev/pps0"` and
`GPSD_OPTIONS="-n"`.

[PyGPSClient](https://github.com/semuconsulting/PyGPSClient) is a Python/Linux
alternative to U-Blox "U-center" (windows only), `pip install pygpsclient`.


### CHRONY

`apt install chrony`

For chrony to use GPS+PPS as reference, add in "/etc/chrony/chrony.conf":
```
refclock SHM 0 refid NMEA offset 0.000 precision 1e-3 poll 0 filter 3
refclock PPS /dev/pps0 refid PPS lock NMEA offset 0.0 poll 3 trust
```

To use chrony as a NTP server _only_, remove all refclocks/sources and add:
```
local stratum 10
allow 192.168.1.0/24
```


### DS3231

Add `dtoverlay=i2c-rtc,ds3231` to "/boot/firmware/config.txt".

`i2cdetect` shows 2 addresses: 0x68 is DS3231 (UU if module loaded) and 0x57 for
the EEPROM chip.

More info [here](https://github.com/rgl/rtc-i2c-ds3231-rpi) or 
[there](https://trevilly.com/ajout-dun-module-rtc-au-raspberry-pi/)...

`apt install util-linux-extra` to install "hwclock".


### PTP

**todo** ptp4l, phc2sys, phc_ctl


---

## Links

[here](https://github.com/jfargentino/ppsd)

[Allan tools](https://github.com/aewallin/allantools)

A [RPi PTP server repo](https://github.com/parlaynu/pi5-timeserver-gps-pps)<br>
A [RPi NTP server repo](https://github.com/Kreeblah/DietPiTimeServer)<br>
Yet another [RPi NTP server repo](https://github.com/tiagofreire-pt/rpi_uputronics_stratum1_chrony)<br>
[In this link](https://austinsnerdythings.com/2025/11/24/worlds-most-stable-raspberry-pi-81-better-ntp-with-thermal-management/) there is temperature compensation.<br>
