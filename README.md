# PPSD

ppsd is a PPS Daemon: do statistics on a PPS device to adjust for clock drift and offset.

---

## Needs

A propperly configured PPS device.

GPSD for "timeref" (`apt install libgps-dev`)

`git clone https://github.com/jfargentino/ppsd.git`

Build with "make" and a decent C compiler, only "math.h" needed.

---

## Run

All applications provide a quick "-h" help.

***One master only***, If setting clock, stop NTP, chrony, timesyncd and the like.

**TODO** systemd start scripts.

### timeref

`timeref -s` to roughly set the system time from GPSD.

### ppsd

`ppsd -N 64 -n 8` Evaluates drift every 64 PPS and offset every 8 PPS.

`ppsd -D 1000000 -o -500000000 -O +500000000` to adjust for drift and offset.

**TODO** `ppsd` need root even without adjusting the clock, probably because
of PPS opening/setting, chowning "/dev/pps0" do "dialout" group do nothing...

### adjtimex

`adjtimex` is "adjtimex (2)" terminal interface using ppb and ns for units.

`adjtimex -f 20000` to adjust the clock frequency by 20000ppb (20ppm).

### tools

`ppsd_plot.sh`, `ppsd_hist.sh` and a couple of OCTAVE/MATLAB scripts.

**TODO** update ppsd.sh !

---

## How to on RPI

Adding `nohz=off` to "/boot/firmware/cmdline.txt" make no arm... on my RPI5,
std dev goes from 700ns down to 300ns !

**WHY** dtoverlay=disable-bt
 
**TODO** measuring temperature (`vcgencmd measure_temp`)

**TODO** running on 1 CPU (IRQ and app ?) to avoid ISR cache flush ?

**TODO** dtoverlay=i2c-rtc,ds3231


### PPS

Setting PPS input (PIN7 GPIO4):
add `dtoverlay=pps-gpio,gpiopin=4` in "/boot/firmware/config.txt"

Add `pps-gpio` to "/etc/modules" ?

Optional: `apt install pps-tools`


### GPS

Setting UART (PIN8 GPIO14 and PIN10 GPIO15):
add `enable_uart=1` in "/boot/firmware/config.txt"

Then run raspi-config to disable login on UART, UART device is ttyAMA0.

`apt install gpsd gpsd-clients gpsd-tools libgps-dev`

Edit "/etc/default/gpsd" to add `DEVICES="/dev/ttyAMA0 /dev/pps0"` and
`GPSD_OPTIONS="-n"`.


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

---

## Links

[here](https://github.com/jfargentino/ppsd)

[Allan tools](https://github.com/aewallin/allantools)

A [RPi NTP server repo](https://github.com/parlaynu/pi5-timeserver-gps-pps)

Another [RPi NTP server repo](https://github.com/Kreeblah/DietPiTimeServer)

Yet another [RPi NTP server repo](https://github.com/tiagofreire-pt/rpi_uputronics_stratum1_chrony)

[In this link](https://austinsnerdythings.com/2025/11/24/worlds-most-stable-raspberry-pi-81-better-ntp-with-thermal-management/) there is temperature compensation.



