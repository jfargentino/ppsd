#include "ppsd.h"
#include "pps_helper.h"
#include "adjtimex_helper.h"
#include "timespec_helper.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************
 *
 *****************************************************************************/
struct estimate_t {
    struct timespec ts;
    long long offset_ns;
    long long drift_ppb;
    long long stddev_ns;
};

static void estimate_set (struct estimate_t * est,
                          struct timespec const * ts,
                          struct pps_stats_t const * stats,
                          unsigned int win) {
    est->ts = *ts;
    long double drift_ppb = pps_stats_drift_ppb(stats, win);
    long double stddev_ns = 0.0l;
    long double mean_ns = pps_stats_mean(stats,
                                         &stddev_ns,
                                         win);
    // Offset estimate is not the mean !
    if (win <= 0) {
        win = pps_stats_length(stats);
    }
    mean_ns += (win/2 + 1) * drift_ppb;
    est->offset_ns = roundl(mean_ns);
    est->drift_ppb = roundl(drift_ppb);
    est->stddev_ns = roundl(stddev_ns);
}

static long long estimate_get (struct estimate_t const * est,
                               struct timespec const * ts) {
    if (NULL == ts) {
        return est->offset_ns;
    }
    long double ns = timespec_diff_ns(ts, &est->ts);
    long long s = roundl(ns / 1e9l);
    return est->offset_ns + s*est->drift_ppb;
}

/*****************************************************************************
 *
 *****************************************************************************/
struct ppsd_t {
    struct pps_t * _;
    struct timespec timeref;
    struct timespec timestamp;
    long hw_offset_ns;
    struct pps_stats_t * drift_stats;
    long long cum_drift_ppb;
    struct pps_stats_t * off_stats;
    long long cum_off_ns;
    unsigned long count;
    unsigned long outliers;
    struct estimate_t est;
    struct timex tx;
};

static struct ppsd_t _PPSD_ = {
    ._ = NULL,
    .timeref = { .tv_sec = 0, .tv_nsec = 0 },
    .timestamp = {  .tv_sec = 0, .tv_nsec = 0 },
    .hw_offset_ns = 0L,
    .drift_stats = NULL,
    .cum_drift_ppb = 0LL,
    .off_stats = NULL,
    .cum_off_ns = 0LL,
    .count = 0UL,
    .outliers = 0UL,
    .est = { { 0 } },
    .tx = { 0 }
};

FILE * ppsdout = NULL;
FILE * ppsderr = NULL;
FILE * ppsddbg = NULL;

#define STDOUT ppsdout
#define STDERR ppsderr
#define STDDBG ppsddbg
#include "slog.h"

/*****************************************************************************
 *
 *****************************************************************************/
static struct ppsd_t * _ppsd_open(char const * path,
                                  bool capture_assert,
                                  long hw_offset_ns) {
    pps_stdout = ppsdout;
    pps_stderr = ppsderr;
    pps_stddbg = ppsddbg;
    pps_stats_out = ppsdout;
    pps_stats_err = ppsderr;
    pps_stats_dbg = ppsddbg;
    _PPSD_._ = pps_open(path, capture_assert);
    if ( _PPSD_._ == NULL) {
        return NULL;
    }
    //clock_gettime(CLOCK_REALTIME, &_PPSD_.timeref);
    _PPSD_.hw_offset_ns = hw_offset_ns;
    return &_PPSD_;
}

static unsigned long ppsd_stats_init(struct ppsd_t * ppsd,
                                     unsigned int drift_pps_nb,
                                     unsigned int off_pps_nb) {
    ass(ppsd->drift_stats == NULL);
    ass(ppsd->off_stats == NULL);
    ppsd->count = 0UL;
    ppsd->outliers = 0UL;
    ppsd_set_timeref(ppsd, NULL);
    if (drift_pps_nb > 0u) {
        ppsd->drift_stats = pps_stats_ctor(drift_pps_nb);
        pps_stats_reset(ppsd->drift_stats, true);
    }
    if (off_pps_nb > 0u) {
        ppsd->off_stats = pps_stats_ctor(off_pps_nb);
        pps_stats_reset(ppsd->off_stats, true);
    }
    return (drift_pps_nb > off_pps_nb) ? drift_pps_nb : off_pps_nb;
}

struct ppsd_t * ppsd_open(char const * path,
                          bool capture_assert,
                          long hw_offset_ns,
                          unsigned int drift_pps_count,
                          unsigned int offset_pps_count) {
    struct ppsd_t * ppsd = _ppsd_open(path, capture_assert, hw_offset_ns);
    if (NULL == ppsd) {
        return NULL;
    }
    adjtimex_snapshot(&ppsd->tx);
    ppsd->cum_drift_ppb = adjtimex_get_freq();
    fcmt(ppsderr, "%s\n", "ADJTIMEX at start:");
    timex_flog(ppsderr, &ppsd->tx);
    // TODO check return
    ppsd_stats_init(ppsd, drift_pps_count, offset_pps_count);
    return ppsd;
}

static int ppsd_stats_release(struct ppsd_t * ppsd) {
    if (ppsd->drift_stats != NULL) {
        pps_stats_dtor(ppsd->drift_stats);
        ppsd->drift_stats = NULL;
        return 0;
    }
    if (ppsd->off_stats != NULL) {
        pps_stats_dtor(ppsd->off_stats);
        ppsd->off_stats = NULL;
    }
    return 1;
}

void ppsd_close(struct ppsd_t * ppsd) {
    ppsd_stats_release(ppsd);
    pps_close(ppsd->_);
    _PPSD_._ = NULL;
}

/*****************************************************************************
 *
 *****************************************************************************/
time_t ppsd_set_timeref(struct ppsd_t * ppsd,
                        struct timespec const * timeref) {
    if (timeref == NULL) {
        // round next PPS timestamp to use as reference
        ppsd->timeref.tv_sec = 0;
        ppsd->timeref.tv_nsec = 0;
    } else {
        ppsd->timeref = *timeref;
    }
    return ppsd->timeref.tv_sec;
}

static struct timespec const * ppsd_timeref(struct ppsd_t const * ppsd) {
    return &ppsd->timeref;
}

static long long ppsd_offset_ns(struct ppsd_t const * ppsd) {
    return timespec_diff_ns(&ppsd->timestamp, &ppsd->timeref);
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_update(struct ppsd_t * ppsd, 
                long double predict_ns,
                long double dist2predict_max_ns,
                unsigned int options) {

    int ret = pps_get_timestamp(ppsd->_, &ppsd->timestamp);
    if (ret < 0) {
        slogerr("Get PPS error \"%s\" (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (ppsd->timeref.tv_sec == 0) {
        // 1st since timeref reset
        slogdbg("%s\n", "timeref reset");
        ppsd->timeref.tv_sec = ppsd->timestamp.tv_sec;
        slogdbg("timeref reset %lds\n",  ppsd->timeref.tv_sec);
        ppsd->timeref.tv_nsec = 0;
        if (ppsd->timestamp.tv_nsec >= ns_per_s / 2) {
            ppsd->timeref.tv_sec ++;
        }
    } else {
        ppsd->timeref.tv_sec ++;
    }
    ppsd->timestamp.tv_nsec -= ppsd->hw_offset_ns;
    timespec_norm(&ppsd->timestamp);
    
    // outlier filtering
    long long pps_off_ns = ppsd_offset_ns(ppsd);
    long double d2p = fabsl(pps_off_ns - predict_ns);
    bool outlier = (dist2predict_max_ns > 0.0L) && (d2p > dist2predict_max_ns);

    // stats update and print
    if (outlier) {
        ppsd->outliers ++;
        slogdbg("outlier #%lu: offset %+lldns, estimate %+.0Lfns +/-%.0Lfns\n",
                ppsd->outliers, pps_off_ns, predict_ns, dist2predict_max_ns);
        fcmt(ppsdout, "%ld, %+9lld\n",
             ppsd_timeref(ppsd)->tv_sec,
             ppsd_offset_ns(ppsd));
    } else {
        ppsd->count ++;
        if (ppsd->off_stats != NULL) {
            (void)pps_stats_update(ppsd->off_stats,
                                   ppsd_timeref(ppsd),
                                   pps_off_ns);
            pps_stats_flast(ppsdout, ppsd->off_stats, options);
        }
        if (ppsd->drift_stats != NULL) {
            (void)pps_stats_update(ppsd->drift_stats,
                                   ppsd_timeref(ppsd),
                                   pps_off_ns + ppsd->cum_off_ns);
            // TODO print drift stat on its own fd to check it
            if (ppsd->off_stats == NULL) {
                pps_stats_flast(ppsdout, ppsd->drift_stats, options);
            }
        }
    }

    return outlier ? 0 : 1;
}

/*****************************************************************************
 *
 *****************************************************************************/
static void _update_adjtimex_fd(void) {
    adjtimex_stdout = ppsdout;
    adjtimex_stderr = ppsderr;
    adjtimex_stddbg = ppsddbg;
}

// max_drift_ppb to 0 to bypass
int ppsd_adj_drift_ppb(struct ppsd_t * ppsd, long max_drift_ppb) {
    
    long long drift_ppb = ppsd->est.drift_ppb;
    long long appb = (drift_ppb > 0) ? (+drift_ppb) : (-drift_ppb);
    if (appb > max_drift_ppb) {
        // max_drift_ppb == 0 -> BYPASS
        if (max_drift_ppb > 0) {
            slogerr("%s|%+lldppb| > maximum allowed %ldppb\n",
                    SLOG_CMT_STR, drift_ppb, max_drift_ppb);
        }
        return 0;
    }
    
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    long freq_ppb = adjtimex_get_freq();
    int ret = adjtimex_adj_freq(-drift_ppb);
    // TODO check cumulative drift == adjtimex correction
    ppsd->cum_drift_ppb += drift_ppb;
    adjtimex_snapshot(&ppsd->tx);
    fcmt(ppsdout, "Adjusting freq %+ldppb by %+lldppb = %+ldppb return %d\n",
         freq_ppb, -drift_ppb, adjtimex_get_freq(), ret);
    return ret;
}

static long ppsd_tick_us(long * smooth_min_ppb) {

    long tick_us = adjtimex_get_tick(NULL);
    // if tick is 10000 (10ms), +/-1 is +/-100ppm 
    // TODO if the clock is so bad we need to adjust tick to compensate,
    // then tick_us % 1000 == 0 not true anymore !
    if ( (tick_us % 1000) || (tick_us <= 1000) || (tick_us > 1000000) ) {
        slogout("Invalid tick value %+ld !\n", tick_us);
        return -1;
    }
    
    // smooth_min_ppb is the nb of ns we can win/lose in 1s for 1us tick change
    // If tick_us = 10*1000 (10ms), smooth_min_ppb = 100*1000 = 100ppm
    // 10s to correct 1ms @ 100ppm
    *smooth_min_ppb = (1000*1000*1000) / tick_us;
    // tick_Hz is the tick frequency in Hz
    //long tick_Hz = (1000*1000) / tick_us;
    slogdbg("TICK %ldus tick adjust +/-%ldppb\n",
            tick_us, *smooth_min_ppb);

    return tick_us;
}

int ppsd_adj_offset_ns(struct ppsd_t * ppsd,
                       long min_offset_ns,
                       long max_offset_ns,
                       unsigned int options) {
    if (min_offset_ns > max_offset_ns) {
        // BYPASS
        return 0;
    }
    long long offset_ns = estimate_get(&ppsd->est, NULL);
    long long stddev_ns = ppsd->est.stddev_ns;
    long long K = 2ll;
    ass(stddev_ns >= 0);
    if ( (offset_ns > -stddev_ns/K) && (offset_ns < +stddev_ns/K) ) {
        fcmt(ppsdout,
             "Offset |%+lldns| < std dev/%lld = %lldns, no offset adj.\n",
             offset_ns, K, stddev_ns/K);
        return 0;
    }
    if ( (offset_ns < -500*1000*1000) || (offset_ns > +500*1000*1000) ) {
        fcmt(ppsdout, "Offset |%+lldns| > 500ms, no offset adj.\n",
             offset_ns);
        return 0;
    }
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    
    int ret = 0;

    if ( (min_offset_ns == max_offset_ns)
            || (offset_ns < min_offset_ns)
            || (offset_ns > max_offset_ns) ) {
        // Abrupt clock setting
        fcmt(ppsdout, "Correcting %+lldns offset\n", offset_ns);
        ret = adjtimex_set_offset(-offset_ns);
        ppsd->cum_off_ns += offset_ns;
        slogdbg("cumulative offset %+lldns\n", ppsd->cum_off_ns);
    } else {
        // smooth clock setting
        long smooth_min_ppb = 0;
        long tick_us = ppsd_tick_us(&smooth_min_ppb);
        // smooth_min_ppb = +/-100ppm per default
        // smooth_max_ppb = +/-1% -> +/-10ms/s
        long tickadj_max_us = 100;
        long smooth_max_ppb = tickadj_max_us * smooth_min_ppb;
        long aoffset_ns = (offset_ns > 0) ? (+offset_ns) : (-offset_ns);
        long s0 = aoffset_ns / smooth_max_ppb;
        long complement_ns = aoffset_ns % smooth_max_ppb;
        if (s0 > 0) {
            long s = s0;
            tickadj_max_us = (offset_ns > 0)
                                        ? +tickadj_max_us : -tickadj_max_us;
            smooth_max_ppb = (offset_ns > 0)
                                        ? +smooth_max_ppb : -smooth_max_ppb;
            fcmt(ppsdout,
        "Adjusting tick %ld%+ldus (%+ldppb) during %lds for %+lldns offset\n",
                  tick_us, -tickadj_max_us, smooth_max_ppb, s, offset_ns);
            ret = adjtimex_adj_tick(-tickadj_max_us);
            while (s > 0) {
                ppsd->cum_off_ns += smooth_max_ppb;
                slogdbg("cumulative offset %+lldns\n", ppsd->cum_off_ns);
                ppsd_update(ppsd, 0.0l, 0.0l, options);
                s --;
            }
            ret = adjtimex_adj_tick(+tickadj_max_us);
        }
        ass(complement_ns >= 0);
        ass(complement_ns < smooth_max_ppb);
        if (complement_ns > smooth_min_ppb) {
            long tickadj_us = complement_ns / smooth_min_ppb;
            complement_ns %= smooth_min_ppb;
            tickadj_us = (offset_ns > 0) ? +tickadj_us : -tickadj_us;
            fcmt(ppsdout,
        "Adjusting tick %ld%+ldus (%+ldppb) during 1s for %+lldns offset\n",
                 tick_us,
                 -tickadj_us,
                 -tickadj_us*smooth_min_ppb,
                 offset_ns);
            ret = adjtimex_adj_tick(-tickadj_us);
            ppsd->cum_off_ns += tickadj_us * smooth_min_ppb;
            slogdbg("cumulative offset %+lldns\n", ppsd->cum_off_ns);
            ppsd_update(ppsd, 0.0l, 0.0l, options);
            ret = adjtimex_adj_tick(+tickadj_us);
        }
        ass(complement_ns >= 0);
        ass(complement_ns < smooth_min_ppb);
        if (complement_ns > stddev_ns / K) {
            complement_ns = (offset_ns > 0) ? +complement_ns : -complement_ns;
            fcmt(ppsdout,
                 "Adjusting freq by %+ldppb during 1s for %+lldns offset\n",
                 -complement_ns, offset_ns);
            ret = adjtimex_adj_freq(-complement_ns);
            ppsd->cum_off_ns += complement_ns;
            slogdbg("cumulative offset %+lldns\n", ppsd->cum_off_ns);
            ppsd_update(ppsd, 0.0l, 0.0l, options);
            ret = adjtimex_adj_freq(+complement_ns);
        } else {
            fcmt(ppsdout,
                 "Complement %ldns < std dev/%lld = %lldns\n",
                 complement_ns, K, stddev_ns / K);
        }
    }

    adjtimex_snapshot(&ppsd->tx);
    
    return ret;
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_run(struct ppsd_t * ppsd,
             long max_drift_ppb,
             long min_offset_ns,
             long max_offset_ns) {
    
    unsigned int options = PPS_STATS_PRINT
                                   | PPS_STATS_PRINT_ABS_TREF
                                   //| PPS_STATS_PRINT_MEDIAN FIXME
                                   | PPS_STATS_PRINT_MEAN
                                   | PPS_STATS_PRINT_DRIFT
                                   | PPS_STATS_PRINT_STDDEV;

    pps_stats_header(ppsdout, ppsd->drift_stats, options);

    unsigned int off_nb = pps_stats_max_length(ppsd->off_stats);
    unsigned int drift_nb = pps_stats_max_length(ppsd->drift_stats);
    unsigned int pps_nb = (off_nb > drift_nb) ? off_nb : drift_nb;
    if (pps_nb == 0u) {
        slogdbg("%s\n", "Continuous stats");
        pps_nb = INT_MAX;
    }

    // TODO optional filtering !
    unsigned int pps_cnt = 0u;
    int ok = ppsd_update(ppsd, 0.0l, 0.0l, options);
    while ( (ok >= 0) && (pps_cnt < pps_nb) ) {
        if (ok > 0) {
            pps_cnt ++;
            // OFFSET //////////////////////////////////////
            if ( (off_nb > 0) && (pps_cnt % off_nb == 0) ) {
                slogout("%sOffset statistics on %d/%dPPS\n",
                        SLOG_CMT_STR, off_nb, pps_cnt);
                pps_stats_header2(ppsdout, ppsd->off_stats, options);
                slogout("%s", SLOG_CMT_STR);
                pps_stats_fprint(ppsdout, ppsd->off_stats, options);
                estimate_set(&ppsd->est,
                             ppsd_timeref(ppsd),
                             ppsd->off_stats,
                             0);
                if (adjtimex_get_freq() == 0) {
                    slogout("%sNo frequency adjustment yet, do one.\n",
                            SLOG_CMT_STR);
                    ppsd_adj_drift_ppb(ppsd, max_drift_ppb);
                }
                // correct offset and increment cumulator for drift eval
                // FIXME PPS update done for offset correction should count
                ppsd_adj_offset_ns(ppsd, min_offset_ns, max_offset_ns, options);
                pps_stats_reset(ppsd->off_stats, true);
            }
            // DRIFT ///////////////////////////////////////
            if ( (drift_nb > 0) && (pps_cnt % drift_nb == 0) ) {
                slogout("%sDrift statistics on %d/%dPPS\n",
                        SLOG_CMT_STR, drift_nb, pps_cnt);
                pps_stats_header2(ppsdout, ppsd->drift_stats, options);
                slogout("%s", SLOG_CMT_STR);
                pps_stats_fprint(ppsdout, ppsd->drift_stats, options);
                estimate_set(&ppsd->est,
                             ppsd_timeref(ppsd),
                             ppsd->drift_stats,
                             0);
                ppsd_adj_drift_ppb(ppsd, max_drift_ppb);
                pps_stats_reset(ppsd->drift_stats, true);
                slogdbg("cumulative offset %+lldns reset\n",
                        ppsd->cum_off_ns);
                ppsd->cum_off_ns = 0;
            }
        } else {
            // TODO a max nb of outliers to get out
            // TODO if estimate used to filter ?
        }

        ok = ppsd_update(ppsd, 0.0l, 0.0l, options);
    
    }
    
    if (ok < 0) {
        slogerr("%sReturning after %u/%u PPS\n",
                SLOG_CMT_STR, pps_cnt, pps_nb);
        return -1;
    }

    return (int)pps_cnt;
}

