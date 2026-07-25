#ifndef TIMEREF_H
#define TIMEREF_H

#include <time.h>

#include <stdio.h>
extern FILE* timeref_stdout;
extern FILE* timeref_stderr;

#define GPSD_TIMEOUT_S (15*60)

int gpsd_timeref(char const * gpsd_addr,
                 char const * gpsd_port,
                 struct timespec * timeref,
                 struct timespec * timenow);

/*
int sntp_timeref(char const * sntp_addr,
                 char const * sntp_port,
                 struct timespec * timeref,
                 struct timespec * timenow);
*/
#endif // TIMEREF_H

