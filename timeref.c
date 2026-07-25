#include <gps.h> // Need GPSD to run
#include <math.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "timeref.h"
#include "timespec_helper.h"

FILE* timeref_stdout = NULL;
FILE* timeref_stderr = NULL;
#define STDOUT timeref_stdout
#define STDERR timeref_stderr
#include "slog.h"

/*
 *
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
                 struct timespec * timenow) {
    
    // TODO SHM ?
    // TODO man is not clear if we should use gps_open return or errno !
    int err = gps_open(gpsd_addr, gpsd_port, &_gps_data);
    if (0 != err) {
        slogout("GPSD connection error: %s!\n", gps_errstr(err));
        return -1;
    }
    gps_stream(&_gps_data, WATCH_ENABLE | WATCH_JSON, NULL);
    
    // wait for GPS fix
    int gps_fix = 0;
    int to = 0;
    do {
        gps_fix = gpsd_get_fix(&_gps_data, timeref, timenow);
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
    timespec_diff(timenow, timeref, &dt);
    slogcmt("%ld.%09ld, %ld.%09ld, %+lldns\n",
            timeref->tv_sec, timeref->tv_nsec,
            timenow->tv_sec, timenow->tv_nsec,
            timespec2ns(&dt));
    gps_stream(&_gps_data, WATCH_DISABLE, NULL);
    gps_close(&_gps_data);
    return 1;
}

/*
 * /////////////////////////////////////////////////////////////////////////////
 */

#ifdef SNTP_TIMEREF

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
    uint64_t reftim; //  64-bit Reference Timestamp
    uint64_t oritim; //  64-bit originate Timestamp
    uint64_t recitim; //  64-bit receive Timestamp 
    uint64_t trantim; //  64-bit transmit Timestamp; Time at which packet left the server. For most cases, using this gets you pretty accurate time.

    //384 Bytes
};

// This timestap is the difference between 1900 and the unix epoch 1970.
// We subtract this from the trasmit timestamp to get the current time
#define NTP_TIMESTAMP_DELTA 2208988800ull

// Here li = 0, vn = 3, mode = 3, which corresponds to (in binary)
//      li = 00 , vn = 011 , mode = 011 -> 00011011 which is 27 in decimal
#define LI_VN_MODE 27

int sntp_timeref(char const * sntp_addr,
                 char const * sntp_port,
                 struct timespec * timeref,
                 struct timespec * timenow) {

    struct sntp_info_t packet = {0};
    packet.li_vn_mode = LI_VN_MODE;

    struct addrinfo hints;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *result, *p;
    int status = getaddrinfo(sntp_addr, sntp_port, &hints, &result);
    if(status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
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
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }

    int n;
    n = write( sockfd, ( char* ) &packet, sizeof(struct sntp_info_t) );

    if (n < 0){
        fprintf(stderr, "client: error writing to socket\n");
        return -1;
    }

    n = read(sockfd, ( char* ) &packet, sizeof(struct sntp_info_t));

    if (n < 0){
        fprintf(stderr, "client: error reading from socket\n");
        return -1;
    }

    freeaddrinfo(result);
    // converting from network byte order to host byte order
    unsigned char *b = (unsigned char*)&packet.trantim;
    uint32_t trantim = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 |
                       (uint32_t)b[2]  << 8 | (uint32_t)b[3]  << 0;

    time_t actual_time = ( time_t ) ( trantim - NTP_TIMESTAMP_DELTA );

    return actual_time;
}
#endif // SNTP_TIMEREF

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

static long long _once(char const * gpsd_addr, char const * gpsd_port) {
    struct timespec timediff = { 0 };
    struct timespec timestamp = { 0 };
    int ret = gpsd_timeref(gpsd_addr, gpsd_port, &timeref, &timestamp);
    if (ret <= 0) {
        fprintf(stdout, "GPSD %s\n", (ret == 0)?"time out":"error");
        if (ret < 0) {
            fprintf(stdout, "\"%s\" (%d)\n", strerror(errno), errno);
        }
        return 0;
    }
               // FIXME WHY ++ ?!?
    /* FIXME */ timeref.tv_sec ++; /* FIXME */
             // FIXME WHY ++ ?!?
    fprintf(stdout, "GPSD Time %ld.%09ld", timeref.tv_sec, timeref.tv_nsec);
    timespec_diff (&timestamp, &timeref, &timediff);
    long long off_ns = timespec2ns(&timediff);
    if ((off_ns <= -1000000000) || (off_ns >= +1000000000)) {
        double off_s = ((double)(off_ns)) / ns_per_s;
        fprintf(stdout, " (offset %+.3lfs)\n", off_s);
    } else if ((off_ns <= -1000000) || (off_ns >= +1000000)) {
        double off_ms = (1000.0 * off_ns) / ns_per_s;
        fprintf(stdout, " (offset %+.3lfms)\n", off_ms);
    } else {
        fprintf(stdout, " (offset %+lldns)\n", off_ns);
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
    {"gpsd-addr", required_argument, NULL, 'a'},
    {"gpsd-port", required_argument, NULL, 'p'},
    /* TODO PPS parameters
    {"pps-dev", required_argument, NULL, 'P'},
    {"pps-assert", no_argument, NULL, 'A'},
    {"pps-clear", no_argument, NULL, 'C'},
    {"pps-offset", required_argument, NULL, 'O'}, */
    /* Application parameters */
    {"set-clock", no_argument, NULL, 's'},
    {"max-offset", required_argument, NULL, 'o'},
    {"continuous", no_argument, NULL, 'C'},
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    {0,0,0,0}
};

// TODO if a PPS is provided, use ir to set the clock !
int main(int argc, char ** argv) {
    
    timeref_stdout = stdout;
    timeref_stderr = NULL;

    char const * gpsd_addr = "localhost";
    char const * gpsd_port = "2947";
    bool set_clock = false;
    bool cont = false;
    long long max_offset_ns = +500000000;

    char short_opts[255] = {0};
    longopts2shortopts (long_opts, short_opts);
    int opt;
    while ((opt = getopt_long(argc, argv,
                              short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
            case 'a':
            gpsd_addr = optarg;
            break;
            case 'p':
            gpsd_port = optarg;
            break;
            case 's':
            set_clock = true;
            break;
            case 'o':
            max_offset_ns = atol(optarg);
            break;
            case 'C':
            cont = true;
            break;
            case 'v':
            timeref_stderr = stderr;
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

    long long off_ns = _once(gpsd_addr, gpsd_port);
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
        } else {
            fprintf(stdout, "Adjusting time by %+lldns\n", -off_ns);
            if (adjtimex_set_offset(-off_ns) < 0) {
                fprintf(stdout, "adjtimex(SET_OFFSET) return \"%s\" (%d)\n",
                        strerror(errno), errno);
            }
        }
        //exit (EXIT_SUCCESS);
    }

    if (cont) while (off_ns) {
        off_ns = _once(gpsd_addr, gpsd_port);
    }

    exit (EXIT_SUCCESS);
}
#endif // TIMEREF_MAIN
