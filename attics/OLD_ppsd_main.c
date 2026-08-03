/*****************************************************************************
 * Need linuxpps include.
 * Must be run as root for PPS configuration.
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include "adjtimex_helper.h"
#include "ppsd.h"
//#include "version.h"

#define STDOUT stdout
#define STDERR stdout
#define STDDBG stdout
#include "slog.h"

// TODO TODO TODO
// Long stats
// Depending on the drift and offset stddev -> short stats for offset 
// prediction (taking in account the drift + time for correction)
// Then drift correction
// TODO TODO TODO

/******************************************************************************
 *
 ******************************************************************************/
static long adjtimex_tick_info(long * smooth_min_ppb, long * smooth_max_ppb) {

    long tick_us = adjtimex_get_tick(NULL);
    // if tick is 10000 (10ms), +/-1 is +/-100ppm 
    // TODO if the clock is so bad we need to adjust tick to compensate,
    // tick_us % 1000 == 0 not true anymore !
    if ( (tick_us % 1000) || (tick_us <= 1000) || (tick_us > 1000000) ) {
        slogout("Invalid tick value %+ld !\n", tick_us);
        return -1;
    }
    
    // smooth_min_ppb is the nb of ns we can win/lose in 1s for 1us tick change
    *smooth_min_ppb = 1000000000 / tick_us;
    long tick_Hz = 1000000 / tick_us;
    *smooth_max_ppb = 100000000 / tick_Hz;
    slogcmt("TICK %ldus (%ldHz) tick adjust +/-%ldppb to +/-%ldppb\n",
            tick_us, tick_Hz, *smooth_min_ppb, *smooth_max_ppb);

    return tick_us;
}

/******************************************************************************
 * TODO a round must be:
 *     "long" unfiltered stats for the drift evaluation
 *     "short" filtered stats for offset
 * TODO the question is when to adjust for the drift, before or after adjusting
 *      the offset ?
 ******************************************************************************/
static int ppsd_round (struct ppsd_t * ppsd,
                       long pps_count_in,
                       long long drift_adj_max_ppb,
                       long long set_offset_max_ppb,
                       long set_offset_max_s){

    // To check there's no NTP instance
    static struct timex tx_mon = { 0 };
    if (adjtimex_snapshot(&tx_mon)) {
    }

    // Do long stats ...
    unsigned long pps_count = ppsd_do_stats(ppsd, (unsigned)pps_count_in);
    if (pps_count < 2) {
        // TODO
        return -1;
    }

    // ... Long stats done

    // TODO Check there's no NTP instance
    long remaining_ns = adjtimex_remaining();
    if ((remaining_ns) != 0 || (adjtimex_snapshot(&tx_mon) && false)) {
        slogcmt("%c", '\n');
        slogcmt("%s\n", "?!? Someone is playing with adjtimex ?!?");
        adjtimex_log(STDOUT);
        slogcmt("%c", '\n');
        return 0;
    }
    
    // TODO detect instable behaviour
    long long drift_ppb = ppsd_drift_ppb(ppsd);
    long long abs_ppb = (drift_ppb < 0) ? -drift_ppb : +drift_ppb;
    long long offset_ns = 0;
    long long stddev_ns = ppsd_offset_stddev_ns(ppsd, 0, &offset_ns);
    long long offset_predict_ns = offset_ns + (pps_count/2 + 1)*drift_ppb;
    long long abs_ns = (offset_predict_ns < 0) 
                                          ? -offset_predict_ns
                                          : +offset_predict_ns;
    slogcmt("## PREDICTION: %+ldns ###\n", offset_predict_ns);

    // TODO ////////////////////////////////////////////////////////////////////
    //long short_stats_s = 16;
    //ppsd_stats_reset(ppsd, short_stats_s);
    //while (short_stats_s > 0) {
    //    slogcmt("## PREDICTION: %+ldns +/-%ldns ###\n",
    //            offset_predict_ns, 2*stddev_ns);
    //    if (ppsd_update(ppsd, offset_predict_ns, 2*stddev_ns) > 0) {
    //        short_stats_s --;
    //    }
    //    offset_predict_ns += drift_ppb;
    //}
    //ppsd_stats_print(ppsd);
    // TODO ////////////////////////////////////////////////////////////////////

    // TODO offset_ns must be a prediction depending on the drift, not the mean
    // TODO offset_ns is noisy, do filtered short (8s to 32s ?) stats  
    // smooth_min_ppb is the nb of ns we can win/lose in 1s for 1us tick change
    long smooth_min_ppb = 0;
    long smooth_max2_ppb = 0;
    long tick_us = adjtimex_tick_info(&smooth_min_ppb, &smooth_max2_ppb);
    if ((tick_us <= 0) || (smooth_min_ppb <= 0) || (smooth_max2_ppb <= 0)){
        slogout("Invalid tick %+ldus, %+ldppb to %+ldppb\n",
                tick_us, smooth_min_ppb, smooth_max2_ppb);
        return -1;
    }
    long smooth_max_ppb = set_offset_max_ppb;
    if (smooth_max_ppb > smooth_max2_ppb) {
        smooth_max_ppb = smooth_max2_ppb;
    }
    if (smooth_min_ppb > smooth_max_ppb) {
        smooth_min_ppb = smooth_max_ppb;
    }
    int ret = 0;
    if ((smooth_min_ppb > 0) && (smooth_max_ppb > 0)) {
        slogcmt("Tick %+ldus, %+ldppb to %+ldppb\n",
                tick_us, smooth_min_ppb, smooth_max_ppb);
        long nb_s = abs_ns / smooth_max_ppb;
        long tick_adj_us = 0;
        long freq_adj_ppb = 0;
        if (nb_s > set_offset_max_s) {
            slogcmt("Offset %+lldns needs %ldppb correction during %lds.\n",
                    offset_predict_ns, smooth_max_ppb, nb_s);
            nb_s = 0;
            // TODO if offset > +/-500ms
            ret = adjtimex_set_offset(-offset_predict_ns);
            slogcmt("Setting offset by %+lldns returns %d.\n",
                    -offset_predict_ns, ret);
        } else if (nb_s > 0) {
            // The offset still evolve during its correction...
            offset_predict_ns += nb_s * drift_ppb;
            abs_ns = (offset_predict_ns < 0) 
                                        ? -offset_predict_ns
                                        : +offset_predict_ns;
            tick_adj_us = (abs_ns/nb_s) / smooth_min_ppb;
            freq_adj_ppb = (abs_ns - nb_s*tick_adj_us*smooth_min_ppb) / nb_s;
        } else {
            nb_s ++;
            tick_adj_us = abs_ns / smooth_min_ppb;
            freq_adj_ppb = abs_ns - tick_adj_us * smooth_min_ppb;
        }
        if (offset_predict_ns > 0) {
            tick_adj_us = -tick_adj_us;
            freq_adj_ppb = -freq_adj_ppb;
        }
        // TODO backup tick and freq in case one is clipped
        ret = adjtimex_adj_tick_freq(tick_adj_us, freq_adj_ppb);
        slogcmt(
            "Offset %+lldns -> tick=%+ldus and freq+=%+ldppb during %lds.\n",
                offset_predict_ns, tick_adj_us, freq_adj_ppb, nb_s);
        while (nb_s > 0) {
            ppsd_update(ppsd, 0.0L, -1.0L);
            nb_s --;
        }
        if (ret >= 0) {
            adjtimex_adj_tick_freq(-tick_adj_us, -freq_adj_ppb);
        }
    }

    // TODO adjust tick for big drift. But then how to handle offset ?
    // drift_adj_max_ppb because a really big drift is a sign of something
    // going wrong (NTP instance, offset > 1s...). If 0 it disables the
    // frequency adjustment.
    if (abs_ppb < drift_adj_max_ppb) {
        ret = adjtimex_adj_freq(-drift_ppb);
        slogcmt("Adjusting freq by %+lldppb (%lu PPS) returns %+d.\n\n",
                -drift_ppb, pps_count, ret);
    } else {
        slogdbg("%+lldppb >= %+lldppb, skeeping freq adj.\n\n",
                abs_ppb , drift_adj_max_ppb);
    }
   
    return (ret >= 0) ? +1 : -1;
}

/******************************************************************************
 *
 ******************************************************************************/
#define PPS_COUNT_MAX (24L*3600L)

#include "opt.h"
#include "version.h"

static char const * short_descr =
    "Evaluates PPS offset stats, use them to set the clock.";

static struct option long_opts[] = {
    /* PPS parameters */
    {"pps-dev", required_argument, NULL, 'P'},
    {"pps-assert", no_argument, NULL, 'A'},
    {"pps-clear", no_argument, NULL, 'C'},
    {"pps-count", required_argument, NULL, 'N'},
    {"pps-offset", required_argument, NULL, 'O'},
    {"hard-pps", no_argument, NULL, 'H'},
    /* clock adjustment parameters */
    {"set-drift", optional_argument, NULL, 'd'},
    {"set-clock", optional_argument, NULL, 'c'},
    {"set-offset-min", optional_argument, NULL, 'o'},
    {"set-offset-max", optional_argument, NULL, 'm'},
    {"do-once", no_argument, NULL, 'D'},
    {"quiet", no_argument, NULL, 'q'},
    {"verbose", no_argument, NULL, 'v'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    /* END */
    {0,0,0,0}
};

static char const * opts_usage[] = {
    /* PPS parameters */
    "PPS_DEV : PPS device to use, default is /dev/pps0.",
    ": use PPS assert (rising edge).",
    ": use PPS clear (falling edge), default.",
    "PPS_CNT : nb of PPS edges to use, 300 per default.",
    "OFFSET : offset of the PPS edge if known, in ns, none per default.",
    ": kernel PPS discipline (TODO.",
    /* clock adjustment parameters */
    "MAX_DRIFT : allow clock freq adjustment if |drift| < MAX_DRIFT ppb.",
    "DRIFT_MAX : set clock if |drift| is < DRIFT_MAX ppb.",
    "OFFSET_MIN : set_offset if offset > OFFSET_MIN ns, singleshot otherwise.",
    "OFFSET_MAX : set_offset if offset < OFFSET_MAX ns, singleshot otherwise.",
    ": do only once.",
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
    adjtimex_stdout = NULL;
    adjtimex_stderr = NULL;

    slogcmt("%s %s %s\n\n", argv[0], VERSION, COMMIT_DATE);

    /* Arguments ************************************************************/
    char const * pps_path = "/dev/pps0";
    bool pps_capture_assert = false;
    // TODO window depending on the drift
    long pps_count_max_in = 300; // TODO no more negative allowed
    long pps_hw_offset_ns = 0L;
    bool hard_pps = false;
    bool once = false;

    long long drift_adj_max_ppb = 0; // Do not adjust clock freq per default
    long long clock_set_max_ppb = 0; // Do not adjust clock offset
    long long set_offset_min_ns = 0; // FIXME while adjusting not OK 
    long long set_offset_max_ns = 0; // FIXME while adjusting not OK 
    
    char short_opts[255] = {0};
    longopts2shortopts (long_opts, short_opts);
    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
            // PPS parameters ////////////////////////////////////////////////
            case 'P':
            pps_path = optarg;
            break;
            case 'A':
            pps_capture_assert = true;
            break;
            case 'C':
            pps_capture_assert = false;
            break;
            case 'N':
            pps_count_max_in = atol(optarg);
            break;
            case 'O':
            pps_hw_offset_ns = atol(optarg);
            break;
            case 'H':
            hard_pps = true;
            break;
            // Clock freq and offset adjustment parameters ////////////////////
            case 'd':
            if (optarg != NULL) {
                drift_adj_max_ppb = atol(optarg);
            } else {
                drift_adj_max_ppb = 1000*1000; // TODO 1000ppm max ?
            }
            break;
            case 'c':
            if (optarg != NULL) {
                clock_set_max_ppb = atol(optarg);
            } else {
                clock_set_max_ppb = 1000*1000; // TODO 1000ppm max ?
            }
            break;
            case 'o':
            if (optarg != NULL) {
                set_offset_min_ns = atol(optarg);
            } else {
                set_offset_min_ns = 0; // TODO
            }
            break;
            case 'm':
            if (optarg != NULL) {
                set_offset_max_ns = atol(optarg);
            } else {
                set_offset_max_ns = 0; // TODO
            }
            break;
            case 'D':
            once = true;
            break;
            case 'q':
            ppsdout = NULL;
            break;
            case 'v':
            if (ppsderr == stderr) {
                adjtimex_stderr = stderr;
            } else {
                ppsderr = stderr;
            }
            break;
            ///////////////////////////////////////////////////////////////////
            case 'h':
            default:
            print_usage(argv[0], short_descr,
                        long_opts, opts_usage, long_descr);
            exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
            break;
        }
    }

    /*************************************************************************
     * Open PPS.
     *************************************************************************/
    struct ppsd_t * ppsd = ppsd_open(pps_path,
                                     pps_capture_assert,
                                     pps_hw_offset_ns);
    if (NULL == ppsd) {
        slogout("%s\n", "No PPS available !!!");
        exit (EXIT_FAILURE);
    }
    
    // Use current time rounded to a full second as reference
    // Need a valid PPS timestamp
    if (ppsd_update(ppsd, 0.0L, -1.0L) < 0) {
        slogout("%s\n", "1st PPS timeout !!!");
        ppsd_close(ppsd);
        exit (EXIT_FAILURE);
    }
    ppsd_set_timeref(ppsd, NULL);

    /*************************************************************************
     * TODO
     *************************************************************************/
    if (hard_pps) {
        int kd = ppsd_kernel_discipline(ppsd);
        slogcmt(">>> PPS KERNEL DISCIPLINE %d <<<\n", kd);
    }

    /*************************************************************************
     * Now use PPS to get some stats
     *************************************************************************/
    if ((pps_count_max_in < 0) || (pps_count_max_in > PPS_COUNT_MAX)) {
        slogdbg("Using %ld PPS for stats instead of %+ld\n",
                PPS_COUNT_MAX,
                pps_count_max_in);
        pps_count_max_in = PPS_COUNT_MAX;
    }
    
    if (ppsd_stats_init (ppsd, (unsigned) pps_count_max_in) <= 0) {
        exit (EXIT_FAILURE);
    }

    int ok = 0;
    do {
        ok = ppsd_round (ppsd,
                         pps_count_max_in,
                         drift_adj_max_ppb,
                         clock_set_max_ppb,
                         8);
    } while ((ok > 0) && !once);

    /* DONE ******************************************************************/
    ppsd_close(ppsd);
    exit ((ok > 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}

