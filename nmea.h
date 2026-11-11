#ifndef NMEA_H
#define NMEA_H

#include <stdio.h>
extern FILE* nmea_stdout;
extern FILE* nmea_stderr;
extern FILE* nmea_stddbg;
#define STDOUT nmea_stdout
#define STDERR nmea_stderr
#define STDDBG nmea_stddbg
#include "slog.h"

#ifndef UART_DEV_DEFAULT
#define UART_DEV_DEFAULT "/dev/ttyS0"
#endif

int nmea_open(char const * uart_dev, char const * uart_cfg);

void nmea_close (int uart_port);

ssize_t nmea_read_buff(int uart_port,
                       char * buff,
                       ssize_t buff_sz,
                       struct timespec * ts);

struct nmea_coord_t {
    int deg;
    double mn;
};

enum nmea_fix_t {
    NMEA_FIX_ERROR = -1,
    NMEA_FIX_NONE = 0,
    NMEA_FIX_WARN = 1,
    NMEA_FIX_2D = 2,
    NMEA_FIX_3D = 3,
    NMEA_FIX_DGPS,
};

struct nmea_data_t {
    struct timespec trx;
    struct timespec tof; // time of ix
    enum nmea_fix_t fix;
    struct nmea_coord_t latitude;
    struct nmea_coord_t longitude;
    double alt_m;
    double pdop;
    double hdop;
    double vdop;
    int sat_nb;
};

// TODO export deciphered NMEA
ssize_t nmea_sentence(int uart_port,
                      char * buff,
                      ssize_t buff_sz,
                      struct nmea_data_t * data);

#endif // 
