/*****************************************************************************
 * drift.c
 * 
 * jf.argentino@peugeotp4.fr
 *
 * 19/07/2025
 *
 * Use GPSD + LinuxPPS to estimate the clock (CLOCK_REALTIME) drift.
 * Need libgps dev package and linuxpps include.
 * Must be run as root for PPS configuration.
 *
 * compile with:
 * gcc -Wall -Wextra -pedantic
 *              timespec_helper.c ll_stats.c drift.c
 *              -o drift -lgps -lm
 *
 * TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
 * 3 app:
 *     - set the clock using GPSD and PPS, rough setting for big offset,
 *       using adjtimex for little offset
 *     - evalutates drift from 2 timestamped offsets, distant one from file vs
 *       current one (need a timeref ?)
 *     - evaluates drift from many PPS (continuous mode, sliding drift)
 *
 *****************************************************************************/
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include "adjtimex_helper.h"
#include "drift_file.h"
#include "ll_stats.h"
#include "ppsd.h"
#include "timeref.h"
#include "timespec_helper.h"
#include "version.h"

#define STDOUT stdout
#define STDERR stdout
#include "slog.h"

/******************************************************************************
 *
 ******************************************************************************/
static char const * shortopts = "a:p:P:ACN:O:D:sduf:H:l:h";

static struct option longopts[] = {
    /* GPSD and GPS parameters */
    {"gpsd-addr", required_argument, NULL, 'a'},
    {"gpsd-port", required_argument, NULL, 'p'},
    /* PPS parameters */
    {"pps-dev", required_argument, NULL, 'P'},
    {"pps-assert", no_argument, NULL, 'A'},
    {"pps-clear", no_argument, NULL, 'C'},
    {"pps-count", required_argument, NULL, 'N'},
    {"pps-offset", required_argument, NULL, 'O'},
    {"pps-delay", required_argument, NULL, 'D'},
    /* Application parameters */
    {"set-clock", no_argument, NULL, 's'},
    {"adjust-drift", no_argument, NULL, 'd'},
    {"update-file", no_argument, NULL, 'u'},
    {"drift-file", required_argument, NULL, 'f'},
    {"high-limit", required_argument, NULL, 'H'},
    {"low-limit", required_argument, NULL, 'l'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    {0,0,0,0}
};

static char const * opts_usage[] = {
    "GPSD_ADDR: GPSD IP addr to connect to, localhost per default.",
    "GPSD_PORT: GPSD port to connect to, 2947 per default.",
    "PPS_DEV: PPS device to use, default is /dev/pps0.",
    ": use PPS assert (rising edge).",
    ": use PPS clear (falling edge), default.",
    "PPS_CNT: nb of PPS edges to use, 0 per default, max 7x24x3600.",
    "OFFSET: offset of the PPS edge if known, in ns, none per default.",
    "DELAY: induced delay \"time_pps_fetch\" in ns, none per default.",
    ": set the time on the 1st PPS.",
    ": adjust the clock frequency for its measured drift.",
    ": update the drift file.",
    "FILE: use a drift file different from the default one (./drift.txt).",
    ": high limit to use adjtimex.",
    ": low limit to use adjtimex.",
    ": display this help."
};

static char const * usage =
"This program is probably only useful to keep a system clock on track when it\n"
"can access a GPS once every couple of hours or more... But \"chrony\" is\n"
"probably a better option for that !\n"
"\n"
"Wait for a GPS fix to get the current date, then wait for PPS.\n" 
"On the first PPS it abruptly set the clock time then for each following PPS\n"
"it displays the reference time, the PPS timestamp and the difference between\n"
"each in nanoseconds. Displayed times are in UNIX format (offcourse).\n"
"Once the number of PPS is reached, or on a PPS timeout, it displays stats\n"
"on the measured offsets. The more there\'s PPS measured, the more useful\n"
"these stats are.\n"
"Then a clock drift evaluation is calculated by using the 1st PPS timestamp\n"
"and the last line of the drift file. This drift evaluation is then used to\n"
"adjust the clock frequency.\n"
"\n"
"To use this program you must:\n"
"\t-have gpsd installed and propperly running, in doubt check with gpsmon;\n"
"\t-have linuxpps installed and propperly running, in doubt use one of\n"
"\t pps-tools provided;\n"
"\t-have root privilege to access PPS, and obviously setting the clock;\n"
"\t-stop any time synchronizing service like NTPD, CHRONYD, PTP...\n"
"\n"
"One way to use it:\n"
"\t-remove any drift file, or use a new one;\n"
"\t-\"adjtimex -f 0\" to start from a fresh state;\n"
"\t-1st time run it with \"drift -u -d -N 900\" to evaluates and correct\n"
"\t the clock drift;\n"
"\t-then periodicaly run it with \"drift -u -d -s\" to set the clock\n"
"\t time and correct its frequency.\n"
"\n"
"When the drift is low enough you can use \"drift -s -N 300\" and then\n"
"use the mean offset displayed to use as argument of \"--pps-delay\" option\n" 
"when running the program. You must change its sign.\n"
"\n"
"One way to check everything is OK is to compare the drift with the one\n"
"provided by chrony. But be aware the drift change according to the\n"
"temprature (and thus the CPU usage) and whatever you can think about.\n"
"\n"
"TODO:\n"
"\t-non opt arg for the drift file to update, if none no update"
"\t-Read/write /etc/default/adjtimex file.\n"
"\t-sort what should being displayed on stderr or stdout.\n"
"\t-a better heuristic to choose between the file drift and the stats one.\n"
"\t-a way to store/reuse the \"--pps-delay\" (another file ?)\n"
"\t-removing outliers before to calulate offset\'s drift and mean.\n"
"\t-use \"adjtimex\" for less than +/-100ms clock adjustements.\n"
"\t-if no PPS available, use the NMEA date.\n"
"\t-access GPSD with SHM, at least for NMEA, but for PPS too ?\n"
"\t-how to be sure ldattach done ?\n"
"\t-Why do I need to reboot my system to detect PPS if it is hot plugged ?\n"
"\t-after a while, /dev/pps0 disapear with the only revelant message from\n"
"\t dmesg being \'pps pps0: removed\' ! Need to do \n"
"\t \'ldattach -d PPS /dev/ttyS0\' again to get it back! GPSD do not retrieve\n"
"\t it without being relaunched... Maybe same problem than the previous one ?\n"
"\t That\'s why \"ldattach -d\" to investigate...\n"
"\n";

static void print_usage (char const * app_name) {
    (void)printf("%s [OPTIONS]...\n\n", app_name);
    (void)printf("Use GPSD+PPS to set the clock and evaluates its drift.\n\n");
    size_t opt = 0u;
    while (longopts[opt].name) {
        (void)printf(" -%c, --%s %s\n",
                     longopts[opt].val,
                     longopts[opt].name,
                     opts_usage[opt]);
        opt ++;
    }
    (void)printf("\n%s\n", usage);
}

#define PPS_COUNT_MAX (7L*24L*3600L)

int main(int argc, char *argv[]) {
    
    // TODO Measure drift then set time / clock freq and update file
    // TODO to evaluate PPS fetch delay, record 1st PPS timestamp after setting
    // TODO read adjtimex at the beginning then at the end, compare and WARN if
    //      different, it means something like NTP is running.
    // TODO nice (NTPDATE_PRIO);
    // TODO read/write /etc/adjtime file
    // 1st line is :
    //    - drift s/day as float, 10ppm = 0.864s/day
    //    - UNIX time of last adjustment or calibration,
    //    - adjustment status, 0.0 for compatibility
    // 2nd line UNIX time of last calibration, can be 0
    // 3rd line is UTC or LOCAL
    // TODO Remove outliers (every 3PPS ? more ?)
    // TODO Use adjtimex instead of clock_settime for little time differences.
    //      Using a min (negative) and max (positive) to allow different
    //      thresholds when early than late. Thus "-5000000 0" forbid jump in
    //      the future. 

    /* Arguments ************************************************************/
    char const * gpsd_addr = "localhost";
    char const * gpsd_port = "2947";

    char const * pps_path = "/dev/pps0";
    bool pps_capture_assert = false;
    long pps_count_max_in = -1;
    unsigned long pps_count_min = 30u; // TODO min nb of PPS to use stats drift
    long pps_hw_offset_ns = 0L;
    long pps_fetch_delay_ns = 0L;

    char const * drift_file_path = "drift.txt";
    
    bool update_drift_file = false;
    bool set_time = false;
    bool set_drift = false;
    
    //long high_threshold_ns = 0;
    //long low_threshold_ns = 0;
    
    char const * SHARP_STR =
        "############################################################";
    slogcmt("%s\n", SHARP_STR);
    slogcmt("%s %s %s\n\n", argv[0], VERSION, COMMIT_DATE);

    int opt;
    while ((opt = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
        switch (opt) {
            case 'a':
            gpsd_addr = optarg;
            break;
            case 'p':
            gpsd_port = optarg;
            break;
            case 't':
            //gps_timeout_s = atoi(optarg);
            break;
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
            case 'D':
            pps_fetch_delay_ns = atol(optarg);
            break;
            case 's':
            set_time = true;
            break;
            case 'd':
            set_drift = true;
            break;
            case 'u':
            update_drift_file = true;
            break;
            case 'f':
            drift_file_path = optarg;
            break;
            case 'H':
            //high_threshold_ns = atol(optarg);
            break;
            case 'l':
            //low_threshold_ns = atol(optarg);
            break;
            case 'h':
            default:
            print_usage(argv[0]);
            slogcmt("\n%s\n", SHARP_STR);
            exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
            break;
        }
    }

    // To check if anybody play with the clock frequency...
    long clk_corr_beg = adjtimex_get_ppb(NULL);
    long clk_corr_end = clk_corr_beg;
    slogcmt("Current drift correction %+ldppb\n", clk_corr_beg);

    /*************************************************************************
     * 1st get last datas back from the drift file .
     *************************************************************************/
    struct timespec prev_timeref = {0}; // date reference
    struct timespec prev_pps_ts1 = {0}; // after setting the clk
    long long prev_drift_ppb = 0LL;
    long long prev_total_ppb = 0LL;
    int lines_nb = drift_fread (drift_file_path,
                                &prev_timeref,
                                &prev_pps_ts1,
                                &prev_drift_ppb,
                                &prev_total_ppb);

    /*************************************************************************
     * Then we're waiting for GPS fix to get the time reference.
     * TODO this give a rough estimate of the local date if PPS is not
     * TODO available.
     *************************************************************************/
    struct timespec timeref = { 0 };
    struct timespec gps_timestamp = { 0 };
    if (gpsd_timeref(gpsd_addr, gpsd_port, &timeref, &gps_timestamp) <= 0) {
        slogerr("%s\n", "No GPS available !!!");
        slogcmt("%s\n\n", SHARP_STR);
        exit (EXIT_FAILURE);
    }

    /*************************************************************************
     * Once GPS fix done, open PPS.
     *************************************************************************/
    struct ppsd_t * ppsd = ppsd_open(pps_path,
                                     pps_capture_assert,
                                     pps_hw_offset_ns);
    if (NULL == ppsd) {
        // TODO using the rough estimate to set the clock and
        // evaluate the drift ?
        slogerr("%s\n", "No PPS available !!!");
        slogcmt("%s\n\n", SHARP_STR);
        exit (EXIT_FAILURE);
    }
    ppsd_set_timeref(ppsd, &timeref);
    if (ppsd_update(ppsd, -1.0L) < 0) {
        slogerr("%s\n", "1st PPS timeout !!!");
        ppsd_close(ppsd);
        slogcmt("%s\n\n", SHARP_STR);
        exit (EXIT_FAILURE);
    }
    /*************************************************************************
     * Now we can evaluate dritf from file.
     *************************************************************************/
    long file_drift_ppb = 0L;
    if (lines_nb > 0) {
        file_drift_ppb = timespec_drift_ppb(ppsd_timestamp(ppsd),
                                            &prev_pps_ts1,
                                            ppsd_timeref(ppsd),
                                            &prev_timeref);
        struct timespec off = { 0 };
        timespec_diff(&prev_pps_ts1, &prev_timeref, &off);
        long long prev_off_ns = timespec2ns(&off);
        slogcmt("Drift from %ld.%09ld@%ld (%+lldns)...\n",
                prev_pps_ts1.tv_sec,
                prev_pps_ts1.tv_nsec,
                prev_timeref.tv_sec,
                prev_off_ns);
        slogcmt("Drift to %ld.%09ld@%ld (%+lldns)...\n",
                ppsd_timestamp(ppsd)->tv_sec,
                ppsd_timestamp(ppsd)->tv_nsec,
                ppsd_timeref(ppsd)->tv_sec,
                ppsd_offset_ns(ppsd));
        timespec_diff(ppsd_timeref(ppsd), &prev_timeref, &off);
        slogcmt("Drift %+lldns in %lds (%+lldns)...\n",
                ppsd_offset_ns(ppsd) - prev_off_ns,
                ppsd_timeref(ppsd)->tv_sec - prev_timeref.tv_sec,
                timespec2ns(&off));
        slogcmt("Drift is %ldppb\n", file_drift_ppb);
        if (set_drift) {
            clk_corr_end = adjtimex_get_ppb(NULL);
            if (clk_corr_end != clk_corr_beg) {
            slogerr("WARN Drift correction change from %+ldppb to %+ldppb !\n",
                    clk_corr_beg,
                    clk_corr_end);
            }
            if (adjtimex_adjust_ppb(file_drift_ppb) < 0) {
                slogcmt("%s\n\n", SHARP_STR);
                exit (EXIT_FAILURE);
            }
            clk_corr_beg = adjtimex_get_ppb(NULL);
            slogcmt("New drift correction %+ldppb\n\n", clk_corr_beg); 
        }
    }
    
    /*************************************************************************
     * Once drift from file processed, set the clock on the next PPS.
     *************************************************************************/
    if (set_time) {
        if (ppsd_set_clock(ppsd, pps_fetch_delay_ns) < 0) {
            ppsd_close(ppsd);
            slogcmt("%s\n\n", SHARP_STR);
            exit (EXIT_FAILURE);
        }
    //} else if (ppsd_update(ppsd, -1.0L) < 0) {
    //    slogerr("%s\n", "1st PPS timeout !!!");
    //    ppsd_close(ppsd);
    //    exit (EXIT_FAILURE);
    } else {}
    
    /*************************************************************************
     * It is time to update drift records
     *************************************************************************/
    if (update_drift_file) {
        (void)drift_fwrite (drift_file_path,
                            ppsd_timeref(ppsd),
                            ppsd_timestamp(ppsd),
                            file_drift_ppb,
                            adjtimex_get_ppb(NULL));
    }

    /*************************************************************************
     * Now use PPS to get some stats
     *************************************************************************/
    if ((pps_count_max_in < 0) || (pps_count_max_in > PPS_COUNT_MAX)) {
        slogcmt("Using %ld PPS for stats instead of %+ld\n",
		        PPS_COUNT_MAX,
                pps_count_max_in);
        pps_count_max_in = PPS_COUNT_MAX;
    }
    
    unsigned long pps_count_max = (unsigned)pps_count_max_in;
    unsigned long pps_count = ppsd_do_stats(ppsd, pps_count_max);
    long long stats_drift_ppb = ppsd_drift_ppb(ppsd);

    if (pps_count >= pps_count_min) {
        if (set_drift) {
            clk_corr_end = adjtimex_get_ppb(NULL);
            if (clk_corr_end != clk_corr_beg) {
                slogerr("WARN Drift corrected from %+ldppb to %+ldppb !\n",
                        clk_corr_beg,
                        clk_corr_end);
            }
            if (adjtimex_adjust_ppb(stats_drift_ppb) < 0) {
                slogcmt("%s\n\n", SHARP_STR);
                exit (EXIT_FAILURE);
            }
            clk_corr_beg = adjtimex_get_ppb(NULL);
            slogcmt("New drift correction %+ldppb\n\n", clk_corr_beg); 
        }
        /**********************************************************************
         * It is time to update drift records
         **********************************************************************/
        if (update_drift_file) {
            (void)drift_fwrite (drift_file_path,
                                ppsd_timeref(ppsd),
                                ppsd_timestamp(ppsd),
                                stats_drift_ppb,
                                adjtimex_get_ppb(NULL));
        }
    }

    /* DONE ******************************************************************/
    ppsd_close(ppsd);
    slogcmt("%s\n\n", SHARP_STR);
    exit (EXIT_SUCCESS);
}

