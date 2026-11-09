#ifndef NMEA_H
#define NMEA_H

#include <stdio.h>

FILE* nmea_stdout = NULL;
FILE* nmea_stderr = NULL;
FILE* nmea_stddbg = NULL;
#define STDOUT nmea_stdout
#define STDERR nmea_stderr
#define STDDBG nmea_stddbg
#include "slog.h"

#ifndef UART_DEV_DEFAULT
#define UART_DEV_DEFAULT "/dev/ttyS0"
#endif

int nmea_open(char const * uart_dev, char const * uart_cfg);

void nmea_close (int uart_port);

ssize_t nmea_read(int uart_port,
                  char * buff,
                  ssize_t buff_sz,
                  struct timespec * ts);

ssize_t nmea_sentence(int uart_port,
                      char * buff,
                      ssize_t buff_sz,
                      struct timespec * ts);

#endif // 
