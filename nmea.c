#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "nmea.h"

FILE* nmea_stdout = NULL;
FILE* nmea_stderr = NULL;
FILE* nmea_stddbg = NULL;

/*
 * /////////////////////////////////////////////////////////////////////////////
 */
#ifndef UART_DEV_DEFAULT
#define UART_DEV_DEFAULT "/dev/ttyS0"
#endif

static int _nmea_uart_port = -1;

// TODO read txt file
int nmea_open(char const * uart_dev, char const * uart_cfg) {
    
    if (_nmea_uart_port >= 0) {
        return _nmea_uart_port;
    }

    //int mode = O_NOCTTY | O_NONBLOCK;
    //int mode = O_NOCTTY;
    int mode = 0;
    _nmea_uart_port = open(NULL == uart_dev ? UART_DEV_DEFAULT : uart_dev,
                           mode |= (NULL == uart_cfg) ? O_RDONLY : O_RDWR);
    if (_nmea_uart_port < 0) {
        slogerr("Can not open UART dev %s in %s: %s\n",
                NULL == uart_dev ? UART_DEV_DEFAULT : uart_dev,
                NULL == uart_cfg ? "RO" : "RW",
                strerror(errno));
        return -1;
    }
    slogdbg("UART dev %s open in %s\n",
            NULL == uart_dev ? UART_DEV_DEFAULT : uart_dev,
            NULL == uart_cfg ? "RO" : "RW");

    if (uart_cfg != NULL) {
        struct termios tty;

        tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control
        tty.c_iflag &= ~((unsigned)IXON); // Turn off s/w flow ctrl
        tty.c_iflag &= ~((unsigned)IXOFF); // Turn off s/w flow ctrl
        tty.c_iflag &= ~((unsigned)IXANY); // Turn off s/w flow ctrl
        
        // TODO
        // Disable any special handling of received bytes
        // tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);

        tty.c_cflag |= CS8; // 8 bits per byte
        tty.c_cflag &= ~((unsigned)PARENB); // no parity
        tty.c_cflag &= ~((unsigned)CSTOPB); // 1 stop bit
        tty.c_lflag &= ~((unsigned)ICANON);
        tty.c_cc[VTIME] = 10; // Wait for up to 1s (10 deciseconds),
        tty.c_cc[VMIN] = 0; // returning as soon as any data is received.
        tty.c_cc[VMIN] = 1; // returning as soon as any data is received.
        tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines

        if(tcgetattr(_nmea_uart_port, &tty) != 0) {
            slogerr("Can not retrieve %s config: %s\n",
                    NULL == uart_dev ? UART_DEV_DEFAULT : uart_dev,
                    strerror(errno));
        } else {

            cfsetispeed(&tty, B9600);
            cfsetospeed(&tty, B9600);
        }
    }
    return _nmea_uart_port;
}

void nmea_close (int uart_port) {
    if (uart_port < 0) {
        slogerr("%s\n", "No UART open yet");
        return;
    }
    if (uart_port != _nmea_uart_port) {
        slogerr("UART provided %d is not NMEA %d\n",
                uart_port, _nmea_uart_port);
        return;
    }
    // FIXME closing the dev take too long or even hang !
    if (0) {
    // Set the close delay timeout to 0 (units are in hundredths of a second)
        struct serial_struct ser_info;
        if (ioctl(uart_port, TIOCGSERIAL, &ser_info) == 0) {
            ser_info.closing_wait = 0; // 0 = Do not wait at all
            ioctl(uart_port, TIOCSSERIAL, &ser_info);
        }
    }
    if (0) {
        tcflush(uart_port, TCIOFLUSH);
    }
    if (0) {
        close(uart_port);
        _nmea_uart_port = -1;
    }
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 */

/*
 * Returns -1 on error, 0 on timeout, nb of bytes read otherwise.
 * The buff is NUL terminated.
 */
ssize_t nmea_read_buff(int uart_port,
                       char * buff,
                       ssize_t buff_sz,
                       struct timespec * ts) {
    if (buff_sz <= 0) {
        return -1;
    }
    ssize_t n = read(uart_port, buff, (unsigned)buff_sz);
    if (ts != NULL) {
        clock_gettime(CLOCK_REALTIME, ts);
    }
    if (n < 0) {
        slogerr("UART%d read error: %s\n", uart_port, strerror(errno));
        return -1;
    }
    if (n == 0) {
        slogerr("UART%d read timeout\n", uart_port);
        return 0;
    }
    if (n >= buff_sz) {
        // TODO maybe by handling this we could read txt file ?
        slogerr("UART%d read overrun\n", uart_port);
        buff[buff_sz-1] = '\0';
        n = -1;
    } else {
        buff[n] = '\0';
    }
    slogdbg("%ld bytes received: \"%s\"\n", n, buff);
    return n;
}

static ssize_t _char_idx (char x, char const * str) {
    ssize_t k = 0;
    while ( (str[k] != '\0') && (str[k] != x) ) {
        k ++;
    }
    return (str[k] == x) ? k : -1;
}

// read and drop until the 1st $ encountered
// return -2 if error, -1 if timeout, 0 if no $ read,
// nb of char after $ otherwise
static ssize_t _nmea_drop(int uart_port,
                          char * buff,
                          ssize_t buff_sz,
                          struct timespec * ts) {
    ssize_t n = nmea_read_buff(uart_port, buff, buff_sz, ts);
    if (n <= 0) {
        return n - 1;
    }
    ass(n <= buff_sz);
    ssize_t h = _char_idx('$', buff);
    if (h < 0) {
        slogdbg("%zd bytes dropped\n", n);
        return 0;
    }
    ass(h < n);
    // NMEA sentence start detected //
    slogdbg("\'%c\' at byte %zd/%zd (\'%c\')\n",
            '$', h, n, buff[h]);
    ass(buff[h] == '$');
    // TODO correct the ts from the nb of char !
    for (ssize_t k = 0; k < n - h; k++) {
        buff[k] = buff[k+h];
    }
    buff[n-h] = '\0';
    return n - h;
}

#ifndef NMEA_FIELDS_NB
#define NMEA_FIELDS_NB 32
#endif

#include <stdint.h>

struct nmea_token_t {
    struct timespec t;
    char const * str;
    char const * type;
    ssize_t nb;
    char const * field[NMEA_FIELDS_NB];
    ssize_t field_sz[NMEA_FIELDS_NB];
    ssize_t k;
    uint8_t csum;
};

static ssize_t _token_init (char const * buff,
                            struct timespec const * t,
                            struct nmea_token_t * nmea) {
    if (buff[0] != '$') {
        slogerr("\'%s\' does not start with \'%c\'\n", buff, '$');
        return -1;
    }
    nmea->str = buff;
    if (t != NULL) {
        nmea->t = *t;
    } else {
        nmea->t.tv_sec = 0;
    }
    nmea->type = buff + 1;
    nmea->k = 1;
    nmea->csum = 0x00;
    nmea->nb = 0;
    for (size_t nb = 0u; nb < NMEA_FIELDS_NB; nb++) {
        nmea->field[nb] = NULL;
        nmea->field_sz[nb] = 0;
    }
    return 1;
}

static ssize_t _tokenize (struct nmea_token_t * nmea) {
    ssize_t k = nmea->k;
    while ( (nmea->str[k] != '\0') && (nmea->str[k] != '*') ) {
        nmea->csum ^= (uint8_t)nmea->str[k];
        if (nmea->str[k] == ',') {
            nmea->field[nmea->nb] = &nmea->str[k+1];
            if (nmea->nb > 0) {
                nmea->field_sz[nmea->nb-1]
                    = nmea->field[nmea->nb] - nmea->field[nmea->nb-1] - 1;
                slogdbg("%.*s[%zd]:%.*s\n",
                        5, nmea->type,
                        nmea->nb-1,
                        (int)nmea->field_sz[nmea->nb-1],
                        nmea->field[nmea->nb-1]);
            } else {
                // 1st ',' should be at nmea->str[6]
                if (k != 6) {
                    slogerr("NMEA type \"%.*s\" ?\n", (int)k, nmea->str);
                }
            }
            nmea->nb ++;
        }
        k ++;
    }
    nmea->k = k;
    if (nmea->str[k] != '*') {
        // end of buffer before end of NMEA
        slogerr("\'%s\' does not end with \'%c\' in %zu bytes\n",
                nmea->str, '*', k);
        ass(nmea->str[nmea->k] == '\0');
        return 1; // read again
    }
    // if here end of the NMEA sentence detected
    ass(nmea->str[nmea->k] == '*');
    if (nmea->nb > 0) {
        nmea->field_sz[nmea->nb-1] = &nmea->str[k] - nmea->field[nmea->nb-1];
        slogdbg("%.*s[%zd]:%.*s\n",
                5, nmea->type,
                nmea->nb-1,
                (int)nmea->field_sz[nmea->nb-1],
                nmea->field[nmea->nb-1]);
    }
    return 0;
}

static uint8_t _nibble (char c) {
    uint8_t x = (uint8_t)c;
    if ( (c >= '0') && (c <= '9') ) {
        return x - '0';
    }
    if ( (c >= 'a') && (c <= 'f') ) {
        return x - 'a' + 10;
    }
    if ( (c >= 'A') && (c <= 'F') ) {
        return x - 'A' + 10;
    }
    return 0xFF;
}

static ssize_t _token_csum (struct nmea_token_t * nmea) {
    if (nmea->str[nmea->k] != '*') {
        return -1;
    }
    if (nmea->str[nmea->k+1] == '\0') {
        slogerr("\'%.*s\' missing csum\n", 5, nmea->type);
        return 2;
    }
    if (nmea->str[nmea->k+2] == '\0') {
        slogerr("\'%.*s\' 1 byte missing for csum\n", 5, nmea->type);
        return 1;
    }
    uint8_t msb = _nibble(nmea->str[nmea->k+1]);
    uint8_t lsb = _nibble(nmea->str[nmea->k+2]);
    // if msb or lsb 0xFF
    uint8_t csum = (msb << 4) | lsb;
    nmea->k += 2;
    slogdbg ("%.*s containing %zd fields, csum 0x%02X/0x%02X\n",
             5, nmea->type, nmea->nb, nmea->csum, csum);
    if (nmea->csum != csum) {
        slogerr("%.*s csum error 0x%02X vs 0x%02X\n",
                 (int)nmea->k, nmea->type, nmea->csum, csum);
        return -1;
    }
    return 0;
}

#define NMEA_TEST_TOKEN 0 // FIXME
static ssize_t _nmea_read_token(int uart_port,
                                char * buff,
                                ssize_t buff_sz,
                                struct nmea_token_t * token) {
    struct timespec t = { 0 };
    ssize_t n = _nmea_drop(uart_port,
                           buff,
                           NMEA_TEST_TOKEN ? NMEA_TEST_TOKEN : buff_sz,
                           &t);
    ssize_t cunt = 0;
    while ( (n == 0) && (cunt < buff_sz/*TODO*/) ) {
        n = _nmea_drop(uart_port,
                       buff,
                       NMEA_TEST_TOKEN ? NMEA_TEST_TOKEN : buff_sz,
                       &t);
        cunt ++;
    }
    if (n <= 0) return n;
    ssize_t total = n;
    ssize_t m = _token_init(buff, &t, token);
    ssize_t c = _tokenize(token);
    cunt = 0;
    while ( (c > 0) && (token->k < buff_sz) && (cunt < buff_sz/*TODO*/) ) {
        slogdbg("No tail in %zu bytes\n", token->k);
        ass(buff[token->k] == '\0');
        n = nmea_read_buff(uart_port,
                           &buff[token->k],
                           NMEA_TEST_TOKEN ? NMEA_TEST_TOKEN
                                           : buff_sz - token->k,
                           NULL);
        if (n <= 0) {
            // a timeout in the middle is an error
            return -1;
        }
        total += n;
        c = _tokenize(token);
        cunt ++;
    }
    // if buffer full ?
    if (token->k >= buff_sz) {
        ass(token->k == buff_sz);
        ass(total == buff_sz);
        return -1;
    }
    ass(c == 0);
    c = _token_csum(token);
    cunt = 0;
    while ( (c > 0) && (token->k < buff_sz) && (cunt < 4)) {
        slogdbg("No csum in %zu bytes\n", token->k);
        ass(buff[token->k] == '\0');
        n = nmea_read_buff(uart_port,
                           &buff[token->k],
                           NMEA_TEST_TOKEN ? NMEA_TEST_TOKEN
                                           : buff_sz - token->k,
                           NULL);
        if (n <= 0) {
            // a timeout in the middle is an error
            return -1;
        }
        total += n;
        c = _token_csum(token);
        cunt ++;
    }
    // if buffer full ?
    if (token->k >= buff_sz) {
        ass(token->k == buff_sz);
        ass(total == buff_sz);
        return -1;
    }
    
    return c == 0 ? total : -1; // TODO check theres no $
}

static void _nmea_token_print (struct nmea_token_t const * nmea, ssize_t nb) {
    if ( (nb >= 0) && (nb < nmea->nb) ) {
        slogerr("%.*s[%2zd]:%.*s\n",
                5, nmea->type, nb,
                (int)nmea->field_sz[nb],
                nmea->field[nb]);
    } else {
        if (nmea->t.tv_sec != 0) {
            slogerr("<%ld.%09ld>", nmea->t.tv_sec, nmea->t.tv_nsec);
        }
        slogerr("%.*s[%2zu]:", 5, nmea->type, nmea->nb);
        for (ssize_t k = 0; k < nmea->nb; k++) {
            if (nmea->field_sz[k] > 0) {
                slogerr("%.*s,",
                        (int)nmea->field_sz[k], nmea->field[k]);
            }
        }
        slogerr("0x%02X\n", nmea->csum);
    }
}

///////////////////////////////////////////////////////////////////////////////

enum nmea_type_t {
    NMEA_ANY = -1,
    NMEA_GGA,
    NMEA_RMC,
    NMEA_GSA,
    NMEA_GLL,
    NMEA_TYPE_NB
};

static char const * NMEA_TYPE[NMEA_TYPE_NB] = {
    "GGA",
    "RMC",
    "GSA",
    "GLL",
};

static bool _nmea_is_type(struct nmea_token_t const * nmea,
                          enum nmea_type_t type) {
    if (strncmp(&nmea->type[2], NMEA_TYPE[type], 3) == 0) {
        return true;
    }
    return false;
}

static enum nmea_type_t _nmea_type (struct nmea_token_t const * nmea) {
    if (_nmea_is_type(nmea, NMEA_GGA)) {
        return NMEA_GGA;
    }
    if (_nmea_is_type(nmea, NMEA_RMC)) {
        return NMEA_RMC;
    }
    if (_nmea_is_type(nmea, NMEA_GSA)) {
        return NMEA_GSA;
    }
    if (_nmea_is_type(nmea, NMEA_GLL)) {
        return NMEA_GLL;
    }
    return NMEA_ANY;
}

static int nmea_tm (char const * time,
                    char const * date,
                    struct tm * tm_ref) {
    sscanf(time, "%2d%2d%2d",
           &tm_ref->tm_hour,
           &tm_ref->tm_min,
           &tm_ref->tm_sec);
    if (time[6] == '.') {
        // TODO msec
    }
    if (date != NULL) {
        sscanf(date, "%2d%2d%2d",
               &tm_ref->tm_mday,
               &tm_ref->tm_mon,
               &tm_ref->tm_year);
        tm_ref->tm_mon --;
        if (tm_ref->tm_year < 80) {
            tm_ref->tm_year += 100;
        }
    }
    return 1;
}

static time_t nmea_time (char const * time, char const * date) {
    struct tm tm_ref = { 0 };
    nmea_tm(time, date, &tm_ref);
    return timegm(&tm_ref);
}

static int nmea_coord (char const * s, char h, struct nmea_coord_t * coord) {
    if (s[4] == '.') {
        // latitude
        coord->deg = (s[0]-'0')*10 + s[1]-'0';
        coord->mn = atof(&s[2]);
        if (h == 'S') {
            coord->deg = -coord->deg;
        } else if (h != 'N') {
            return -1;
        }
        slogdbg(" %.*s,%c -> latitude  %+3ddeg %.6fmn\n",
                10, s, h, coord->deg, coord->mn);
    } else if (s[5] == '.') {
        // longitude
        coord->deg = (s[0]-'0')*100 + (s[1]-'0')*10 + s[2]-'0';
        coord->mn = atof(&s[3]);
        if (h == 'E') {
            coord->deg = -coord->deg;
        } else if (h != 'W') {
            return -1;
        }
        slogdbg("%.*s,%c -> longitude %+3ddeg %.6fmn\n",
                11, s, h, coord->deg, coord->mn);
    } else {
        return -1;
    }
    return 1;
}

static int nmea_rmc (struct nmea_token_t const * token,
                     struct nmea_data_t * data) {
    if (token->nb < 9) {
        return -1;
    }
    struct timespec t;
    t.tv_sec = nmea_time(token->field[0], token->field[8]);
    t.tv_nsec = 0;
    int valid = (token->field[1][0] == 'A') ? 1 : 0;
    slogdbg("%s RMC date %ld received at %ld\n",
            valid > 0 ? "Valid" : "Unvalid",
            t.tv_sec, token->t.tv_sec);
    data->trx = token->t;
    data->tof = t;
    nmea_coord(token->field[2], token->field[3][0], &data->latitude);
    nmea_coord(token->field[4], token->field[5][0], &data->longitude);
    return valid;
}

static int nmea_gll (struct nmea_token_t const * token,
                     struct nmea_data_t * data) {
    if (token->nb < 6) {
        return -1;
    }
    // TODO valid field 5
    nmea_coord(token->field[0], token->field[1][0], &data->latitude);
    nmea_coord(token->field[2], token->field[3][0], &data->longitude);
    struct tm t;
    nmea_tm(token->field[4], NULL, &t);
    // TODO valid field 5
    return 1;
}

static int nmea_gga (struct nmea_token_t const * token,
                     struct nmea_data_t * data) {
    if (token->nb < 9) {
        return -1;
    }
    struct tm t;
    nmea_tm(token->field[0], NULL, &t);
    nmea_coord(token->field[1], token->field[2][0], &data->latitude);
    nmea_coord(token->field[3], token->field[4][0], &data->longitude);
    // TODO fix field[5]
    data->sat_nb = atoi(token->field[6]);
    data->hdop = atof(token->field[7]);
    data->alt_m = atof(token->field[8]);
    data->trx = token->t;
    return 1;
}

static int nmea_gsa (struct nmea_token_t const * token,
                     struct nmea_data_t * data) {
    if (token->nb < 17) {
        return -1;
    }
    data->pdop = atof(token->field[14]);
    data->hdop = atof(token->field[15]);
    data->vdop = atof(token->field[16]);
    data->trx = token->t;
    slogdbg("GSA pdop %.3f, hdop %.3f, vdop %.3f\n",
            data->pdop, data->hdop, data->vdop);
    return 1;
}

void nmea_data_print(struct nmea_data_t const * data) {
    if (data->trx.tv_sec) {
        slogout("<%ld.%09ld>", data->trx.tv_sec, data->trx.tv_nsec);
    }
    if (data->tof.tv_sec) {
        if (data->tof.tv_nsec > 0) {
            slogout("[%ld.%09ld] ", data->tof.tv_sec, data->tof.tv_nsec);
        } else {
            slogout("[%ld] ", data->tof.tv_sec);
        }
    }
    // TODO FIX
    if (data->latitude.deg || (data->latitude.mn > 0.0001)) {
        slogout("LAT  %+3ddeg %.6fmn, ", 
                data->latitude.deg, data->latitude.mn);
    }
    if (data->longitude.deg || (data->longitude.mn > 0.0001)) {
        slogout("LONG %+3ddeg %.6fmn, ",
                data->longitude.deg, data->longitude.mn);
    }
    if ((data->alt_m > +0.001) || (data->alt_m < -0.001) ) {
        slogout("ALT %.3fm, ", data->alt_m);
    }
    if ((data->pdop > +0.001) || (data->pdop < -0.001) ) {
        slogout("pdop %.3f, ", data->pdop);
    }
    if ((data->hdop > +0.001) || (data->hdop < -0.001) ) {
        slogout("hdop %.3f, ", data->hdop);
    }
    if ((data->vdop > +0.001) || (data->vdop < -0.001) ) {
        slogout("vdop %.3f, ", data->vdop);
    }
    if (data->sat_nb) {
        slogout("%d sat", data->sat_nb);
    }
    slogout("%c", '\n')
}

ssize_t nmea_sentence(int uart_port,
                      char * buff,
                      ssize_t buff_sz,
                      struct nmea_data_t * data) {
    struct nmea_token_t x = { 0 };
    ssize_t n = _nmea_read_token(uart_port, buff, buff_sz, &x);
    if (data != NULL) {
        data->trx = x.t;
    } else {
        x.t.tv_sec = 0;
    }
    if (n <= 0) return n;
    _nmea_token_print(&x, -1);
    int ok = 0;
    switch (_nmea_type(&x)) {
        case NMEA_RMC:
        ok = nmea_rmc(&x, data);
        break;
        case NMEA_GGA:
        ok = nmea_gga(&x, data);
        break;
        case NMEA_GSA:
        ok = nmea_gsa(&x, data);
        break;
        case NMEA_GLL:
        ok = nmea_gll(&x, data);
        break;
        default:
        //ass(_nmea_type(&x) == NMEA_ANY);
        break;
    }
    if (ok > 0) {
        nmea_data_print(data);
    }
    return n;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 * tty -F /dev/ttyUSB0 -icrnl -echo
 */
#ifdef NMEA_MAIN

#include "opt.h"

static struct option long_opts[] = {
    /* Application parameters */
    {"set-clock", no_argument, NULL, 's'},
    {"force-clock", no_argument, NULL, 'f'},
    {"max-offset", required_argument, NULL, 'o'},
    {"continuous", no_argument, NULL, 'c'},
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    {0,0,0,0}
};

// TODO if a PPS is provided, use it !
int main(int argc, char ** argv) {
    
    nmea_stdout = stdout;
    nmea_stderr = NULL;
    nmea_stddbg = NULL;

    bool set_clock = false;
    bool force_clock = false;
    bool cont = false;
    long long max_offset_ns = +500000000;

    char short_opts[255] = {0};
    longopts2shortopts (long_opts, short_opts);
    int opt;
    while ((opt = getopt_long(argc, argv,
                              short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
            case 's':
            set_clock = true;
            break;
            case 'f':
            set_clock = true;
            force_clock = true;
            break;
            case 'o':
            max_offset_ns = atol(optarg);
            break;
            case 'c':
            cont = true;
            break;
            case 'v':
            if (nmea_stderr == stderr) {
                nmea_stddbg = stderr;
            } else {
                nmea_stderr = stderr;
            }
            break;
            case 'q':
            nmea_stdout = NULL;
            break;
            case 'h':
            default:
            print_usage(argv[0], NULL, long_opts, NULL, NULL);
            exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
            break;
        }
    }
    
    char const * dev = NULL;
    if (optind < argc) {
        dev = argv[optind];
        optind ++; 
    }
    int nmea_dev = nmea_open(dev, NULL);
    if (nmea_dev < 0) {
        exit (EXIT_FAILURE);

    }
    char buff[256];
    struct nmea_data_t data;
    ssize_t n = nmea_sentence(nmea_dev, buff, 256, 1?&data:NULL);
    while ( (n >= 0) && (cont) ) {
        n = nmea_sentence(nmea_dev, buff, 256, 1?&data:NULL);
    }
    nmea_close(nmea_dev);
    exit (EXIT_SUCCESS);
}

#endif // NMEA_MAIN

