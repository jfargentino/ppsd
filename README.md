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


## Links

[todo](https://github.com/parlaynu/pi5-timeserver-gps-pps)

<https://austinsnerdythings.com/2025/02/14/revisiting-microsecond-accurate-ntp-for-raspberry-pi-with-gps-pps-in-2025/>

