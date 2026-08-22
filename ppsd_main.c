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
    {"nb-drift", required_argument, NULL, 'N'},
    {"nb-offset", required_argument, NULL, 'n'},
    
    /* PPS parameters */
    {"pps-assert", no_argument, NULL, 'A'},
    {"pps-clear", no_argument, NULL, 'C'},
    {"pps-hw-offset", required_argument, NULL, 'H'},

    /* drift parameters */
    {"drift-max", optional_argument, NULL, 'D'},
    
    /* offset parameters */
    {"offset-min", optional_argument, NULL, 'o'},
    {"offset-max", optional_argument, NULL, 'O'},

    /* verbosity */
    {"quiet", no_argument, NULL, 'q'},
    {"verbose", no_argument, NULL, 'v'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    /* END */
    {0,0,0,0}
};

static char const * opts_usage[] = {
    ": do once, drift and offset, then exit.",
    "NB : nb of PPS for drift evaluation, 96s per default.",
    "NB : nb of PPS for offset evaluation, 8s per default.",
    /* PPS parameters */
    ": use PPS assert (rising edge).",
    ": use PPS clear (falling edge), default.",
    "OFFSET : offset of the PPS edge if known, in ns, none per default.",
    /* drift parameters */
    "DRIFT_MAX : over this value (in ppb), no drift correction.",
    /* offset parameters */
    "OFFSET_MIN : under this value (in ns), no offset correction.",
    "OFFSET_MAX : over this value (in ns), no offset correction.",
    /* verbosity */
    ": less verbose.",
    ": more verbose, twice for debug traces.",
    /* show usage */
    ": display this help.",
    /* END */
    "\0"
};

static char const * long_descr =
"Displays the reference time, the PPS timestamp and the difference between\n"
"each in nanoseconds. Displayed times are in UNIX format (off course).\n"
"Once the number of PPS is reached, or on a PPS timeout, it displays\n"
"statisticss on the measured offsets. These can be used to correct the CLK\n"
"frequency and/or offset.\n"
"\n"
"To use this program you must:\n"
"\t-have linuxpps installed and propperly running, in doubt use one of\n"
"\t pps-tools provided;\n"
"\t-have root privilege to access PPS, and obviously setting the clock;\n"
"\t-if doing correction, stop any time synchronizing service like NTPD,\n"
"\t CHRONYD, PTP...\n"
"\n"
"TODO: A 2nd argument to record in a file instead of stdout.\n"
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
    
    long nb_drift = 64;
    long nb_offset = 8;
    bool once = false;

    long long drift_max_ppb = 0; // Do not adjust clock freq per default
    long long offset_min_ns = +500*1000*1000;
    long long offset_max_ns = -500*1000*1000;
    
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
            nb_drift = atol(optarg);
            break;
            case 'n':
            nb_offset = atol(optarg);
            break;
            // PPS parameters ////////////////////////////////////////////////
            case 'A':
            pps_capture_assert = true;
            break;
            case 'C':
            pps_capture_assert = false;
            break;
            case 'H':
            pps_hw_offset_ns = atol(optarg);
            break;
            // Drift parameters ///////////////////////////////////////////////
            case 'D':
            if (optarg != NULL) {
                drift_max_ppb = atol(optarg);
            } else {
                drift_max_ppb = 1000*1000;
            }
            break;
            // Offset parameters //////////////////////////////////////////////
            case 'o':
            if (optarg != NULL) {
                offset_min_ns = atol(optarg);
            } else {
                offset_min_ns = -500*1000*1000;
            }
            break;
            case 'O':
            if (optarg != NULL) {
                offset_max_ns = atol(optarg);
            } else {
                offset_max_ns = +500*1000*1000;
            }
            break;
            // Verbosity //////////////////////////////////////////////////////
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
            printf ("Invalid option %c\n", opt);
            // FALLTHROUGH
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

    struct ppsd_t * ppsd = ppsd_open(pps_path,
                                     pps_capture_assert,
                                     pps_hw_offset_ns,
                                     nb_drift,
                                     nb_offset);
    if (ppsd == NULL) exit (EXIT_FAILURE);
    
    do {
        int ret = ppsd_run (ppsd, 
                            drift_max_ppb,
                            offset_min_ns,
                            offset_max_ns);
        if (ret < 0) {
            once = true;
        }
    } while (once == false);

    ppsd_close(ppsd);
    exit (EXIT_SUCCESS);
}

