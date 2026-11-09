#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

FILE* nmea_stdout = NULL;
FILE* nmea_stderr = NULL;
FILE* nmea_stddbg = NULL;
#define STDOUT nmea_stdout
#define STDERR nmea_stderr
#define STDDBG nmea_stddbg
#include "slog.h"

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
ssize_t nmea_read(int uart_port,
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
    ssize_t k = 0u;
    while ( (str[k] != '\0') && (str[k] != x) ) {
        k ++;
    }
    return (str[k] == x) ? k : -1;
}

static ssize_t _nmea_drop(int uart_port,
                          char * buff,
                          ssize_t buff_sz,
                          struct timespec * ts) {
    ssize_t n = nmea_read(uart_port, buff, buff_sz, ts);
    if (n <= 0) {
        return n;
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

static ssize_t _nmea_until(int uart_port,
                           char * buff,
                           ssize_t buff_sz,
                           struct timespec * ts) {
    ssize_t t = _char_idx('*', buff);
    ssize_t e = _char_idx('\0', buff);
    ass(e >= 0);
    ass(e < buff_sz);
    ass(t < e);
    while ( (t < 0) && (e < buff_sz) ) {
        slogdbg("No tail in %zd bytes\n", e);
        ssize_t n = nmea_read(uart_port, &buff[e], buff_sz-e, ts);
        if (n <= 0) {
            // a timeout in the middle is an error
            return -1;
        }
        t = _char_idx('*', buff);
        e += n;
    }
    if (t < 0) {
        // buffer full without *
        return -1;
    }
    ass(e >= 0);
    ass(e < buff_sz);
    ass(t < e);
    // NMEA sentence end detected //
    slogdbg("\'%c\' at byte %zd/%zd (\'%c\')\n",
            '*', t, e, buff[t]);
    ass(buff[t] == '*');
    ass(buff[e] == '\0');
    return t;
}

static ssize_t _nmea_sentence(int uart_port,
                              char * buff,
                              ssize_t buff_sz,
                              struct timespec * ts) {
    ssize_t n = _nmea_drop(uart_port, buff, buff_sz, ts);
    if (n <= 0) return n;
    ssize_t t = _nmea_until(uart_port, buff, buff_sz, NULL);
    if (t <= 0) return t;
    ssize_t e = _char_idx('\0', buff);
    ass(e >= 0);
    ass(e < buff_sz);
    ass(t < e);
    slogdbg("%zd/%zd bytes consumed\n", t, e);
    if (e > t + 2) {
        // TODO csum on buff[t+1] and buff[t+2]
        slogdbg("%.*s\n", (int)t+3, buff);
        // TODO check there's only \n and/or \r
    } else {
        slogerr("%.*s CSUM MISSING !\n", (int)e, buff);
    }
    return t;
}

#ifndef NMEA_FIELDS_NB
#define NMEA_FIELDS_NB 100
#endif

struct nmea_t {
    struct timespec t;
    char const * type;
    ssize_t nb;
    char const * field[NMEA_FIELDS_NB];
    ssize_t field_sz[NMEA_FIELDS_NB];
    char csum[2];
};

static ssize_t _nmea_tok (char const * buff, struct nmea_t * nmea) {
    if (buff[0] != '$') {
        slogerr("\'%s\' does not start with \'%c\'\n", buff, '$');
        return -1;
    }
    nmea->type = buff + 1;
    nmea->nb = 0;
    for (size_t nb = 0u; nb < NMEA_FIELDS_NB; nb++) {
        nmea->field[nb] = NULL;
        nmea->field_sz[nb] = 0;
    }
    size_t k = 1u;
    while ( (buff[k] != '\0') && (buff[k] != '*') ) {
        if (buff[k] == ',') {
            nmea->field[nmea->nb] = &buff[k+1];
            slogdbg("%.*s[%zd]:%.*s...\n",
                    5, nmea->type, nmea->nb, 16, nmea->field[nmea->nb]);
            if (nmea->nb > 0) {
                nmea->field_sz[nmea->nb-1]
                    = nmea->field[nmea->nb] - nmea->field[nmea->nb-1] - 1;
                slogdbg("%.*s[%zd]:%.*s\n",
                        5, nmea->type,
                        nmea->nb-1,
                        (int)nmea->field_sz[nmea->nb-1],
                        nmea->field[nmea->nb-1]);
            } else {
                // 1st ',' should be at buff[6]
                if (k != 6) {
                    slogerr("Misformed NMEA \"%s\"\n", buff);
                }
            }
            nmea->nb ++;
        }
        k ++;
    }
    if (buff[k] != '*') {
        // end of buffer before end of NMEA
        slogerr("\'%s\' does not end with \'%c\' in %zu bytes\n", buff, '*', k);
        ass(buff[k] == '\0');
        return 0;
    }
    if (nmea->nb > 0) {
        nmea->field_sz[nmea->nb-1] = &buff[k] - nmea->field[nmea->nb-1];
        slogdbg("%.*s[%zd]:%.*s\n",
                5, nmea->type,
                nmea->nb-1,
                (int)nmea->field_sz[nmea->nb-1],
                nmea->field[nmea->nb-1]);
    }
    //nmea->nb ++;
    nmea->csum[0] = buff[k+1];
    nmea->csum[1] = buff[k+2];
    slogdbg ("%.*s containing %zd fields\n", 5, nmea->type, nmea->nb);
    return +1;
}

static void _nmea_print (struct nmea_t const * nmea, ssize_t nb) {
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
        slogerr("0x%c%c\n", nmea->csum[0], nmea->csum[1]);
    }
}

ssize_t nmea_sentence(int uart_port,
                      char * buff,
                      ssize_t buff_sz,
                      struct timespec * ts) {
    struct nmea_t x = { 0 };
    ssize_t n = _nmea_sentence(uart_port, buff, buff_sz, ts);
    if (ts != NULL) {
        x.t = *ts;
    }
    if (n <= 0) return n;
    ssize_t m = _nmea_tok(buff, &x);
    if (m <= 0) return m;
    _nmea_print(&x, -1);
    return n;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
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

// TODO if a PPS is provided, use it to set the clock !
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
    struct timespec t;
    ssize_t n = nmea_sentence(nmea_dev, buff, 256, 1?&t:NULL);
    while (n >= 0) {
        n = nmea_sentence(nmea_dev, buff, 256, 1?&t:NULL);
    }
    nmea_close(nmea_dev);
    exit (EXIT_SUCCESS);
}
#endif // TIMEREF_MAIN
