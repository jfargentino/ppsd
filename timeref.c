#include <gps.h> // Need GPSD to run
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "timeref.h"
#include "timespec_helper.h"

FILE* timeref_stdout = NULL;
FILE* timeref_stderr = NULL;
FILE* timeref_stddbg = NULL;
#define STDOUT timeref_stdout
#define STDERR timeref_stderr
#define STDDBG timeref_stddbg
#include "slog.h"

/*
 * /////////////////////////////////////////////////////////////////////////////
 */
static int gpsd_get_fix (struct gps_data_t * gps_data,
                         struct timespec * nmea_time,
                         struct timespec * local_time) {
    // Wait for 1s
    int ret_code = gps_waiting(gps_data, 1000000);
    clock_gettime(CLOCK_REALTIME, local_time);
    if (ret_code <= 0) {
        // TODO timeout
        return ret_code;
    }
#if GPSD_API_MAJOR_VERSION < 7
    ret_code = gps_read(gps_data);
#else
    ret_code = gps_read(gps_data, NULL, 0);
#endif // GPSD_API_MAJOR_VERSION < 7
    if (ret_code <= 0) {
        // TODO read error
        return ret_code;
    }
    if (MODE_SET != (MODE_SET & gps_data->set)) {
        // No TPV data
        return 0;
    }
    
    slogdbg("%s\n", "Tref; EPT; latitude; longitude; Tlocal");
#if GPSD_API_MAJOR_VERSION < 9
    slogdbg("%lf; ", gps_data->fix.time);
    double sec = floor(gps_data->fix.time);
    double nsec = 1e9 * (gps_data->fix.time - sec);
    nmea_time->tv_sec = (long)sec;
    nmea_time->tv_nsec = (long)round(nsec);
#else
    slogdbg("%ld.%09ld; ",
            gps_data->fix.time.tv_sec,
            gps_data->fix.time.tv_nsec);
    *nmea_time = gps_data->fix.time;
#endif // GPSD_API_MAJOR_VERSION < 9
    slogerr("%.6f; %.6f; %.6f; %ld.%09ld\n",
            gps_data->fix.ept, // expected time uncertainty
            gps_data->fix.latitude,
            gps_data->fix.longitude,
            local_time->tv_sec,
            local_time->tv_nsec);

    if (gps_data->fix.mode < MODE_2D) {
        slogdbg("%s\n", "No fix.");
        return 0;
    }
    if (TIME_SET != (TIME_SET & gps_data->set)) {
        slogdbg("%s\n", "Time not set.");
        return 0;
    }
    if (nmea_time->tv_nsec != 0) {
        slogerr("NMEA time not a plain second: %ld.%09ld.\n",
                nmea_time->tv_sec, nmea_time->tv_nsec);
        return 0;
    }
    if (! isfinite(gps_data->fix.latitude) ||
        ! isfinite(gps_data->fix.longitude)) {
        slogdbg("%s\n", "Invalide LAT and/or LONG.");
        return 0;
    }
    return 1;
}

#ifndef GPSD_TIMEOUT_S
    #define GPSD_TIMEOUT_S (15*60)
#endif

// TODO defined as a static to avoid gpsd_timeref stack overflow (29k)
static struct gps_data_t _gps_data = { 0 };

int gpsd_timeref(char const * gpsd_addr,
                 char const * gpsd_port,
                 struct timespec * timeref,
                 struct timespec * timestamp) {
    
    // TODO SHM ?
    // TODO man is not clear if we should use gps_open return or errno !
    int err = gps_open(gpsd_addr == NULL ? GPSD_ADDR_DEFAULT : gpsd_addr,
                       gpsd_port == NULL ? GPSD_PORT_DEFAULT : gpsd_port,
                       &_gps_data);
    if (0 != err) {
        slogout("GPSD connection error: %s!\n", gps_errstr(err));
        return -1;
    }
    gps_stream(&_gps_data, WATCH_ENABLE | WATCH_JSON, NULL);
    
    // wait for GPS fix
    int gps_fix = 0;
    int to = 0;
    do {
        gps_fix = gpsd_get_fix(&_gps_data, timeref, timestamp);
        if (gps_fix <= 0) { to ++; }
    } while ((gps_fix <= 0) && (to < GPSD_TIMEOUT_S));

    if (gps_fix <= 0) {
        slogout("Still no GPSD data after %ds.\n", GPSD_TIMEOUT_S);
        gps_stream(&_gps_data, WATCH_DISABLE, NULL);
        gps_close(&_gps_data);
        return 0;
    }

    slogdbg("%s\n", "Tref; Tgpsd; diff (ns)");
    struct timespec dt = {0};
    timespec_diff(timestamp, timeref, &dt);
    slogcmt("%ld.%09ld, %ld.%09ld, %+lldns\n",
            timeref->tv_sec, timeref->tv_nsec,
            timestamp->tv_sec, timestamp->tv_nsec,
            timespec2ns(&dt));
    gps_stream(&_gps_data, WATCH_DISABLE, NULL);
    gps_close(&_gps_data);
    return 1;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 */
struct sntp_info_t{
    uint8_t li_vn_mode; // li   2 bit code warning of a leap second
                        // vn   3 bit integer version number
                        // mode 3 bit number for protocol mode

    uint8_t stratum; // 8 bit unsigned indicator for stratum
    uint8_t poll_int; // 8 bit unsigned maximum interval between sucessive messages in seconds
    int8_t precision; // 8 bit signed precision of system clock
    int32_t root_delay; // 32 bit signed fixed point total roundtrip delay
    uint32_t root_disp; // 32 bit unsigned indicating the maximum error due to the clock frequency tolerance  
    uint32_t refid; // 32 bits. Reference clock identifier
    uint32_t reftim_sec; //  32-bit Reference Timestamp seconds
    uint32_t reftim_frac; //  32-bit Reference Timestamp fractionnal part
    uint32_t oritim_sec; //  originate Timestamp
    uint32_t oritim_frac;
    uint32_t recitim_sec; //  receive Timestamp 
    uint32_t recitim_frac;
    uint32_t trantim_sec; //  transmit Timestamp; Time at which packet left the server. For most cases, using this gets you pretty accurate time.
    uint32_t trantim_frac;
    //384 Bytes
};

// This timestap is the difference between 1900 and the unix epoch 1970.
// We subtract this from the trasmit timestamp to get the current time
#define NTP_TIMESTAMP_DELTA 2208988800

static time_t ntp2timespec (uint32_t s, uint32_t frac, struct timespec * ts) {
    time_t sec = ntohl(s) - NTP_TIMESTAMP_DELTA;
    long nsec = ((long)ntohl(frac) * 1000000000) >> 32;
    if (ts != NULL) {
        ts->tv_sec = sec;
        ts->tv_nsec = nsec;
    }
    return sec;
}


// Here li = 0, vn = 3, mode = 3, which corresponds to (in binary)
//      li = 00 , vn = 011 , mode = 011 -> 00011011 which is 27 in decimal
#define LI_VN_MODE 27

int sntp_timeref(char const * sntp_addr,
                 char const * sntp_port,
                 struct timespec * timeref,
                 struct timespec * timestamp) {

    struct sntp_info_t packet = {0};
    packet.li_vn_mode = LI_VN_MODE;

    struct addrinfo hints;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *result, *p;
    int status = getaddrinfo(sntp_addr,
                             sntp_port == NULL ? SNTP_PORT_DEFAULT : sntp_port,
                             &hints,
                             &result);
    if(status != 0) {
        slogerr("getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }
    
    int sockfd;
    for(p = result; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }

    if (p == NULL) {
        slogerr("client: %s\n", "failed to connect");
        return -1;
    }

    ssize_t n;
    n = write( sockfd, ( char* ) &packet, sizeof(struct sntp_info_t) );

    if (n < 0){
        slogerr("client: %s\n", "error writing to socket");
        return -1;
    }

    n = read(sockfd, ( char* ) &packet, sizeof(struct sntp_info_t));
    if (n < 0){
        slogerr("client: %s\n", "error reading from socket");
        return -1;
    }
    freeaddrinfo(result);
    ntp2timespec(packet.trantim_sec, packet.trantim_frac, timeref);
    if (NULL != timeref) {
        slogerr("SNTP Tx time %ld.%09ld\n",
                timeref->tv_sec, timeref->tv_nsec);
    }
    if (NULL != timestamp) {
        ntp2timespec(packet.recitim_sec, packet.recitim_frac, timestamp);
        slogerr("SNTP Rx time %ld.%09ld\n",
                timestamp->tv_sec, timestamp->tv_nsec);
    }
    if (timeref_stderr != NULL) {
        int32_t x = ntohl(packet.root_delay);
        double delay = ((double)x / 65536.0f);
        x = ntohl(packet.root_disp);
        double disp = ((double)x / 65536.0f);
        slogerr("SNTP delay %fs, dispersion +/-%fs\n", delay, disp);
    }
    return 1;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 */
#ifndef UART_DEV_DEFAULT
#define UART_DEV_DEFAULT "/dev/ttyS0"
#endif

#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static ssize_t _nmea_read(int uart_port,
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
    return (str[k] == '\0') ? -1 : k;
}

static ssize_t _nmea_raw(int uart_port,
                         char * buff,
                         ssize_t buff_sz,
                         struct timespec * ts) {
    ssize_t tot = 0;
    ssize_t n = _nmea_read(uart_port, buff, buff_sz, ts);
    ssize_t h = -1;
    while ( (n > 0) && (tot < buff_sz) && (h < 0) ) {
        tot += n;
        h = _char_idx('$', buff);
        if (h < 0) {
            n = _nmea_read(uart_port, buff, buff_sz, ts);
        }
    }
    if (n <= 0) {
        // Error or timeout
        return n;
    }
    if (tot >= buff_sz) {
        // plenty of bytes read without NMEA sentence start //
        slogerr("UART%d %ld bytes read without \'%c\'\n",
                uart_port, tot, '$');
        return 0;
    }
    // NMEA sentence start detected //
    ass(buff[h] == '$');
    // TODO correct the ts from the nb of char !
    for (ssize_t k = 0; k < n - h; k++) {
        buff[k] = buff[k+h];
    }
    slogdbg("NMEA sentence start detected \'%s\'\n",
            buff);
    n -= h;
    tot = n;
    ssize_t t = _char_idx('*', buff);
    while ( (n > 0) && (tot < buff_sz) && (t < 0) ) {
        n = _nmea_read(uart_port, buff+tot, buff_sz-tot, NULL);
        if (n > 0) {
            t = _char_idx('$', buff+tot);
            tot += n;
        }
    }
    if (t < 0) {
        // plenty of bytes read without NMEA sentence end //
        slogerr("UART%d %ld bytes read without \'%c\'\n",
                uart_port, tot, '*');
        return -1;
    }
    // TODO use t, tot and n
    ssize_t tt = _char_idx('*', buff);
    slogdbg("NMEA sentence \'%.*s\'\n", (int)tt, buff);
    return tt; // return tt to check for csum and following sentences
}

#ifndef NMEA_FIELDS_NB
#define NMEA_FIELDS_NB 100
#endif

struct nmea_raw_t {
    char const * type;
    ssize_t nb;
    char const * field[NMEA_FIELDS_NB];
    ssize_t field_sz[NMEA_FIELDS_NB];
};

static ssize_t _nmea_tok (char const * buff, struct nmea_raw_t * nmea) {
    if (buff[0] != '$') {
        slogerr("No \'%c\' in \'%s\'\n", '$', buff);
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
            if (nmea->nb > 0) {
                nmea->field_sz[nmea->nb-1]
                    = nmea->field[nmea->nb] - nmea->field[nmea->nb-1] - 1;
            }
            nmea->nb ++;
        }
        k ++;
    }
    if (buff[k] != '*') {
        // end of buffer before end of NMEA
        slogerr("No \'%c\' in \'%s\'\n", '*', buff);
        return 0;
    }
    //nmea->field_sz[nmea->nb-1] = k - nmea->field[nmea->nb-1];
    nmea->nb ++;
    slogdbg ("%*.s containing %zd fields\n", 5, nmea->type, nmea->nb);
    return +1;
}

static void _nmea_print (struct nmea_raw_t const * nmea, ssize_t nb) {
    if ( (nb >= 0) && (nb < nmea->nb) ) {
        slogerr("%.*s[%zd]:%.*s\n",
                5, nmea->type, nb,
                (int)nmea->field_sz[nb],
                nmea->field[nb]);
    } else {
        slogerr("%.*s[%zu]:", 5, nmea->type, nmea->nb);
        for (ssize_t k = 0; k < nmea->nb; k++) {
            if (nmea->field_sz[k] > 0) {
                slogerr("[%zd]%.*s,",
                        k, (int)nmea->field_sz[k], nmea->field[k]);
            }
        }
        slogerr("%c", '\n');
    }
}

#ifndef NMEA_NB
#define NMEA_NB 200
#endif

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

static bool _nmea_is_type(char const * nmea, enum nmea_type_t type) {
    if (strncmp(&nmea[3], NMEA_TYPE[type], 3) == 0) {
        return true;
    }
    return false;
}

static enum nmea_type_t _nmea_type (char const * nmea) {
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

static int nmea_get_fix (int uart_port,
                         struct timespec * timestamp,
                         struct timespec * timeref) {
    #define BUFF_SZ 256
    char buff[BUFF_SZ] = { 0 };
    int valid = 0;
    // TODO timestamp must the one from 1st sentence after PPS...
    ssize_t nmea_sz = _nmea_raw(uart_port, buff, BUFF_SZ, timestamp);
    timeref->tv_sec = 0;
    int nmea_nb = 0;
    while ( (nmea_sz > 0) && (timeref->tv_sec == 0) ) {
        struct nmea_raw_t nmea;
        struct tm tm_ref = { 0 };
        _nmea_tok (buff, &nmea);
        _nmea_print (&nmea, -1);
        switch (_nmea_type(buff)) {
            case NMEA_RMC:
            // timeref
            // TODO centisec ?
            sscanf(nmea.field[0], "%2d%2d%2d",
                   &tm_ref.tm_hour,
                   &tm_ref.tm_min,
                   &tm_ref.tm_sec);
            sscanf(nmea.field[8], "%2d%2d%2d",
                   &tm_ref.tm_mday,
                   &tm_ref.tm_mon,
                   &tm_ref.tm_year);
            tm_ref.tm_mon --;
            if (tm_ref.tm_year < 80) {
                tm_ref.tm_year += 100;
            }
            timeref->tv_sec = timegm(&tm_ref);
            valid = (nmea.field[1][0] == 'A') ? 1 : 0;
            slogout("%s RMC date %ld received at %ld\n",
                    valid > 0 ? "Valid" : "Unvalid",
                    timeref->tv_sec, timestamp->tv_sec);
            break;
            case NMEA_GSA:
            // TODO valid
            case NMEA_GLL:
            case NMEA_GGA:
            default:
            break;
        }
        nmea_nb ++;
        if (nmea_nb > NMEA_NB) {
            slogerr("%d NMEA sentences without timeref\n",
                    nmea_nb);
            nmea_sz = 0;
        } else {
            nmea_sz = _nmea_raw(uart_port, buff, BUFF_SZ, timestamp);
        }
    }
    return nmea_sz > 0 ? valid : -1;
}

static int _nmea_uart_port = -1;

static int _nmea_open(char const * uart_dev,
                      char const * uart_cfg) {
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

static void _nmea_close (int uart_port) {
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

int nmea_timeref(char const * uart_dev,
                 char const * uart_cfg,
                 struct timespec * timeref,
                 struct timespec * timestamp) {
    
    int uart_port = _nmea_open(uart_dev, uart_cfg);

    int fix_nb = 0;
    int valid = nmea_get_fix (uart_port, timestamp, timeref);
    while ( (valid == 0) && (fix_nb < GPSD_TIMEOUT_S) ) {
        slogerr("fix %d %+d\n", fix_nb, valid);
        fix_nb ++;
        valid = nmea_get_fix (uart_port, timestamp, timeref);
    }

    slogerr("fix %d %+d, closing\n", fix_nb, valid);
    _nmea_close(uart_port);
    slogerr("fix %d %+d, closed\n", fix_nb, valid);

    if (valid <= 0) {
        slogout("%s after %d fixes\n",
                (valid < 0) ? "Error" : "Still not valid",
                fix_nb);
    }

    return valid;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 */
#ifdef TIMEREF_MAIN
#include "adjtimex_helper.h"
#include "timespec_helper.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct timespec timeref = { 0 };
static struct timespec timestamp = { 0 };

static long long _once(char const * addr, char const * port) {

    static int ret_nmea = 0;
    static int ret_gpsd = 0;
    static int ret_sntp = 0;
    
    if (addr == NULL) {
        ret_gpsd = 1;
    }
    
    if ((ret_gpsd <= 0) && (ret_sntp <= 0)) {
        ret_sntp = sntp_timeref(addr, port, &timeref, &timestamp);
        if (ret_sntp <= 0) {
            //ret_gpsd = gpsd_timeref(addr, port, &timeref, &timestamp);
            ret_gpsd = nmea_timeref(addr, port, &timeref, &timestamp);
        }
    } else if (ret_gpsd > 0) {
        //ret_gpsd = gpsd_timeref(addr, port, &timeref, &timestamp);
        ret_gpsd = nmea_timeref(addr, port, &timeref, &timestamp);
    } else if (ret_sntp > 0) {
        ret_sntp = sntp_timeref(addr, port, &timeref, &timestamp);
    }
    if ((ret_gpsd <= 0) && (ret_sntp <= 0)) {
        fprintf(stdout,
                "%s:%s is not a GPSD nor NTP server\n",
                addr, port);
        return 0LL;
    }
    if (ret_gpsd > 0) {
        // FIXME WHY ++ ?!?
        timeref.tv_sec ++;
        fprintf(stdout,
                "GPSD Time %ld.%09ld",
                timeref.tv_sec, timeref.tv_nsec);
    } else {
        fprintf(stdout,
                "SNTP Time %ld.%09ld",
                timeref.tv_sec, timeref.tv_nsec);
    }

    struct timespec timediff = { 0 };
    timespec_diff (&timestamp, &timeref, &timediff);
    long long off_ns = timespec2ns(&timediff);
    if ((off_ns <= -1000000000) || (off_ns >= +1000000000)) {
        double off_s = ((double)(off_ns)) / ns_per_s;
        fprintf(stdout, " (offset %+.3lfs)\n", off_s);
    } else if ((off_ns <= -1000000) || (off_ns >= +1000000)) {
        double off_ms = (1000.0l * off_ns) / ns_per_s;
        fprintf(stdout, " (offset %+.3lfms)\n", off_ms);
    } else {
        fprintf(stdout, " (offset %+lldns)\n", off_ns);
    }
    if ( (ret_sntp > 0) && (true) ) {
        usleep((1000000000 - timeref.tv_nsec) / 1000);
    }

    return off_ns;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 */
#include <errno.h>
#include <string.h>
#include "opt.h"

static struct option long_opts[] = {
    /* GPSD and GPS parameters */
    {"addr", required_argument, NULL, 'a'},
    {"port", required_argument, NULL, 'p'},
    /* TODO PPS parameters
    {"pps-dev", required_argument, NULL, 'P'},
    {"pps-assert", no_argument, NULL, 'A'},
    {"pps-clear", no_argument, NULL, 'C'},
    {"pps-offset", required_argument, NULL, 'O'}, */
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
    
    timeref_stdout = stdout;
    timeref_stderr = NULL;
    timeref_stddbg = NULL;

    //char const * addr = "localhost";
    char const * addr = NULL;
    char const * port = NULL;
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
            case 'a':
            addr = optarg;
            break;
            case 'p':
            port = optarg;
            break;
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
            if (timeref_stderr == stderr) {
                timeref_stddbg = stderr;
            } else {
                timeref_stderr = stderr;
            }
            break;
            case 'q':
            timeref_stdout = NULL;
            break;
            case 'h':
            default:
            print_usage(argv[0], NULL, long_opts, NULL, NULL);
            exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
            break;
        }
    }
    
    long long off_ns = 0ll;
    if (optind < argc) {
        while ( (optind < argc) && (off_ns == 0ll) ) {
            addr = argv[optind];
            off_ns = _once(addr, port);
            optind ++; 
        }
    } else {
        off_ns = _once(addr, port);
    }

    if (off_ns == 0) {
        exit (EXIT_FAILURE);
    }
    
    if (set_clock && off_ns) {
        if ((off_ns <= -max_offset_ns) || (off_ns >= +max_offset_ns)) {
            double off_s = ((double)(off_ns)) / ns_per_s;
            fprintf(stdout, "Setting time to %ld.%09ld (%+.3lfs)\n",
                    timeref.tv_sec, timeref.tv_nsec, -off_s);
            if (clock_settime(CLOCK_REALTIME, &timeref) < 0) {
                fprintf(stdout, "clock_settime return \"%s\" (%d)\n",
                        strerror(errno), errno);
            }
        } else if (force_clock) {
            fprintf(stdout, "Adjusting time by %+lldns\n", -off_ns);
            if (adjtimex_set_offset(-off_ns) < 0) {
                fprintf(stdout, "adjtimex(SET_OFFSET) return \"%s\" (%d)\n",
                        strerror(errno), errno);
            }
        }
    }

    if (cont) while (off_ns) {
        off_ns = _once(addr, port);
    }

    exit (EXIT_SUCCESS);
}
#endif // TIMEREF_MAIN
