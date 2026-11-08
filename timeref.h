#ifndef TIMEREF_H
#define TIMEREF_H

#include <time.h>

#include <stdio.h>
extern FILE* timeref_stdout;
extern FILE* timeref_stderr;

#define GPSD_ADDR_DEFAULT "localhost"
#define GPSD_PORT_DEFAULT "2947"
#define GPSD_TIMEOUT_S (15*60)

int gpsd_timeref(char const * gpsd_addr,
                 char const * gpsd_port,
                 struct timespec * timeref,
                 struct timespec * timenow);

#define SNTP_PORT_DEFAULT "ntp"

int sntp_timeref(char const * sntp_addr,
                 char const * sntp_port,
                 struct timespec * timeref,
                 struct timespec * timenow);

#define UART_DEV_DEFAULT "/dev/ttyUSB0"
#define UART_CFG_DEFAULT "384008N1" /*TODO*/

int nmea_timeref(char const * uart_dev,
                 char const * uart_cfg,
                 struct timespec * timeref,
                 struct timespec * timenow);

#endif // TIMEREF_H

