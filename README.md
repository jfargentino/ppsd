# PPSD

ppsd is a PPS Daemon: do statistics on a PPS device to adjust for clock drift and offset.

---

## Needs

A propperly configured PPS device.

GPSD for "timeref"

Build with "make" and a decent C compiler, only "math.h" needed.

---

## Run

All applications provide a quick "-h" help.

***One master only***, stop NTP, chrony, timesyncd and the like.

### `timeref -s`
to roughly set the system time from GPSD.

### `ppsd -D 1000000 -o -500000000 -O +500000000`
to adjust for drift and offset.

***TODO*** `ppsd` need root even without adjusting the clock, probably because
of PPS opening/setting, chowning "/dev/pps0" do "dialout" group do nothing...

### `adjtimex`
is "adjtimex (2)" terminal interface using ppb and ns for units.


---

## How to on RPI

Setting the UART (PIN8 GPIO14 and PIN10 GPIO15):
add "enable_uart=1" in "/boot/firmware/config.txt"
then run raspi-config to disable login on UART
the UART is ttyAMA0

Setting PPS input (PIN7 GPIO4):
add "dtoverlay=pps-gpio,gpiopin=4" in "/boot/firmware/config.txt"
add "pps-gpio" to "/etc/modules" ?

apt install gpsd gpsd-clients gpsd-tools libgps-dev

apt install pps-tools

apt install chrony ?


---



## Links

[todo](https://github.com/parlaynu/pi5-timeserver-gps-pps)

[todo](https://github.com/Kreeblah/DietPiTimeServer)

[todo](https://github.com/tiagofreire-pt/rpi_uputronics_stratum1_chrony)

<https://austinsnerdythings.com/2025/02/14/revisiting-microsecond-accurate-ntp-for-raspberry-pi-with-gps-pps-in-2025/>

[Allan tools](https://github.com/aewallin/allantools)

