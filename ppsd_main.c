/*****************************************************************************
 * Need linuxpps include.
 * Must be run as root for PPS configuration.
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppsd.h"

#define STDOUT stdout
#define STDERR stdout
#define STDDBG stdout
#include "slog.h"

/******************************************************************************
 * TODO
 *  1. cpupower frequency-set -g performance
 *  2. cat /proc/interrupts | grep -i tty
 *     if IRQ4 on CPU0
 *     echo 1 | sudo tee /proc/irq/4/smp_affinity
 *
 * On JETSON:
 * nvpmodel -m 0
 * jetson_clocks
 * cat /proc/interrupts | grep -i pps
 * if IRQ118 on CPU3
 * echo 8 | sudo tee /proc/irq/118/smp_affinity
 * 
 ******************************************************************************/
#include "opt.h"
#include "version.h"

static char const * short_descr =
    "Evaluates PPS offset stats, use them to set the clock.\n"
    "Use /dev/pps0 per default, provides another dev as argument.";

static struct option long_opts[] = {

    {"once", no_argument, NULL, '1'},
    {"nb", required_argument, NULL, 'N'},
    
    /* PPS parameters */
    {"pps-assert", no_argument, NULL, 'A'},
    {"pps-clear", no_argument, NULL, 'C'},
    //{"pps-offset", required_argument, NULL, 'H'},

    /* drift parameters */
    {"drift-min", optional_argument, NULL, 'd'},
    {"drift-max", optional_argument, NULL, 'D'},
    
    /* offset parameters */
    {"offset-corr", no_argument, NULL, 'c'},
    {"offset-min", optional_argument, NULL, 'o'},
    {"offset-max", optional_argument, NULL, 'O'},

    /* verbosity */
    {"sorted", no_argument, NULL, 's'},
    {"quiet", no_argument, NULL, 'q'},
    {"verbose", no_argument, NULL, 'v'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    /* END */
    {0,0,0,0}
};

static char const * opts_usage[] = {
    ": do once, drift and offset, then exit.",
    "NB : nb of PPS for statistics, 96 per default.",
    /* PPS parameters */
    ": use PPS assert (rising edge).",
    ": use PPS clear (falling edge), default.",
    //"OFFSET : offset of the PPS edge if known, in ns, none per default.",
    /* drift parameters */
    "DRIFT_MIN : under this value (in ppb), no drift correction.",
    "DRIFT_MAX : over this value (in ppb), no drift correction.",
    /* offset parameters */
    " : offset correction, none per default.",
    "OFFSET_MIN : under this value (in ns), no offset correction.",
    "OFFSET_MAX : over this value (in ns), no offset correction.",
    /* verbosity */
    ": print PPS offsets sorted.",
    ": less verbose.",
    ": more verbose.",
    /* show usage */
    ": display this help.",
    /* END */
    "\0"
};

static char const * long_descr =
"Displays the reference time, the PPS timestamp and the difference between\n"
"each in nanoseconds. Displayed times are in UNIX format (offcourse).\n"
"Once the number of PPS is reached, or on a PPS timeout, it displays stats\n"
"on the measured offsets.\n"
"\n"
"To use this program you must:\n"
"\t-have linuxpps installed and propperly running, in doubt use one of\n"
"\t pps-tools provided;\n"
"\t-have root privilege to access PPS, and obviously setting the clock;\n"
"\t-stop any time synchronizing service like NTPD, CHRONYD, PTP...\n"
"\n"
"TODO:\n"
"\t-sort what should being displayed on stderr or stdout.\n"
"\t-removing outliers before to calulate offset\'s drift and mean.\n"
"\t-use \"adjtimex\" for less than +/-100ms clock adjustements.\n"
"\t-Why do I need to reboot my system to detect PPS if it is hot plugged ?\n"
"\t-after a while, /dev/pps0 disapear with the only revelant message from\n"
"\t dmesg being \'pps pps0: removed\' ! Need to do \n"
"\t \'ldattach -d PPS /dev/ttyS0\' again to get it back! GPSD do not retrieve\n"
"\t it without being relaunched... Maybe same problem than the previous one ?\n"
"\t That\'s why \"ldattach -d\" to investigate...\n"
"\n";

int main(int argc, char *argv[]) {
    
    // TODO read adjtimex at the beginning then at the end, compare and WARN if
    //      different, it means something like NTP is running.
    // TODO nice (NTPDATE_PRIO);
    // TODO Remove outliers (every 3PPS ? more ?)
    
    ppsdout = stdout;
    ppsderr = NULL;
    ppsddbg = NULL;

    slogcmt("%s %s %s\n\n", argv[0], VERSION, COMMIT_DATE);

    /* Arguments ************************************************************/
    char pps_path[256] = { '\0' };
    strncpy(pps_path, "/dev/pps0", 255);
    bool pps_capture_assert = false;
    long pps_hw_offset_ns = 0L;
    
    long pps_nb = 96;
    bool once = false;

    long long drift_max_ppb = 0; // Do not adjust clock freq per default
    long long drift_min_ppb = 0;

    bool offset_corr = false;
    long long offset_min_ns = -500000000; // -500ms -> always adjust when neg
    long long offset_max_ns = +1000000; // if > +1ms, coarse offset correction
    
    unsigned int stat_options = PPS_STATS_PRINT
                                   | PPS_STATS_PRINT_ABS_TREF
                                   //| PPS_STATS_PRINT_MEDIAN FIXME
                                   | PPS_STATS_PRINT_MEAN
                                   | PPS_STATS_PRINT_DRIFT
                                   | PPS_STATS_PRINT_STDDEV;

    char short_opts[255] = {0};
    longopts2shortopts (long_opts, short_opts);
    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
            case '1':
            once = true;
            break;
            case 'N':
            pps_nb = atol(optarg);
            break;
            // PPS parameters ////////////////////////////////////////////////
            case 'A':
            pps_capture_assert = true;
            break;
            case 'C':
            pps_capture_assert = false;
            break;
            /*case 'H':
            pps_hw_offset_ns = atol(optarg);
            break;*/
            // Drift parameters ///////////////////////////////////////////////
            case 'D':
            if (optarg != NULL) {
                drift_max_ppb = atol(optarg);
            } else {
                drift_max_ppb = 1000*1000;
            }
            break;
            case 'd':
            if (optarg != NULL) {
                drift_min_ppb = atol(optarg);
            } else {
                drift_min_ppb = 50;
            }
            break;
            // Offset parameters //////////////////////////////////////////////
            case 'c':
            offset_corr = true;
            break;
            case 'o':
            if (optarg != NULL) {
                offset_min_ns = atol(optarg);
            } else {
                offset_min_ns = -10*1000*1000;
            }
            break;
            case 'O':
            if (optarg != NULL) {
                offset_max_ns = atol(optarg);
            } else {
                offset_max_ns = +10*1000*1000;
            }
            break;
            // Verbosity //////////////////////////////////////////////////////
            case 's':
            stat_options |= PPS_STATS_PRINT_SORTED | PPS_STATS_PRINT_INFO;
            break;
            case 'q':
            ppsdout = NULL;
            break;
            case 'v':
            if (ppsderr == stderr) {
                ppsddbg = ppsderr;
            } else {
                ppsderr = stderr;
            }
            break;
            ///////////////////////////////////////////////////////////////////
            default:
	    printf ("invalide option %c\n", opt);
            case 'h':
            print_usage(argv[0], short_descr,
                        long_opts, opts_usage, long_descr);
            exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
            break;
        }
    }

    if (optind < argc) {
        strncpy(pps_path, argv[optind], 255);
    }

    struct ppsd_t * ppsd = ppsd_init(pps_path,
                                     pps_capture_assert,
                                     pps_hw_offset_ns,
                                     pps_nb);
    if (ppsd == NULL) exit (EXIT_FAILURE);
    
    // TODO
    // 16s or so of stats, compensate the offset
    // 96s or so of stats (no reset), compensate the drift
    bool stat_reset = false;
    do {
        unsigned long ret = ppsd_do_stat(ppsd,
                                         stat_reset,
                                         pps_nb,
                                         stat_options);
        if (ret == 0u) {
            once = true;
        } else {
            stat_reset = false;
            long long offset_ns = ppsd_est_offset_ns(ppsd, NULL);
            long long drift_ppb = ppsd_est_drift_ppb(ppsd);
            long long stddev_ns = ppsd_est_stddev_ns(ppsd);
            fcmt(ppsdout, "ESTIMATION %+lldns@%+lldppb, stddev %lldns\n",
                 offset_ns, drift_ppb, stddev_ns);
            // Offset correction
            if (offset_corr) {
                if ( (offset_ns > offset_min_ns)
                        && (offset_ns < offset_max_ns) ) {
                    ppsd_adj_offset_ns(ppsd, 200*1000, stat_options);
                } else {
                    // Coarse correction
                    ppsd_set_offset_ns(ppsd);
                }
                stat_reset = true;
            }
            // Drift Correction
            long long appb = (drift_ppb < 0) ? (-drift_ppb) : (+drift_ppb);
            if ( (appb < drift_max_ppb) && (appb > drift_min_ppb) ) {
                ppsd_adj_freq_ppb(ppsd);
                stat_reset = true;
            } else if (drift_min_ppb || drift_max_ppb) {
                fcmt(ppsdout,
                     "DRIFT %+lldppb not in +/-[%lld, %lld]ppb, no freq adj.\n",
                     drift_ppb, drift_min_ppb, drift_max_ppb);
            }
        }
    } while (once == false);

    ppsd_close(ppsd);
    exit (EXIT_SUCCESS);
}

