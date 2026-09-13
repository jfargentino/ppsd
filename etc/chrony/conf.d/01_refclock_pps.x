# To measure the PPS against the ref used to sync
#refclock PPS /dev/pps0 lock NMEA refid PPS noselect

#refclock PPS /dev/pps0 lock NMEA refid PPS width 0.100 offset 0.100 precision 1e-5
#refclock PPS /dev/pps0 lock NMEA refid PPS local
#refclock PPS /dev/pps0:clear lock NMEA refid PPS delay 0.5 local
