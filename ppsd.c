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

static struct timespec const * ppsd_timeref(struct ppsd_t const * ppsd) {
    return &ppsd->timeref;
}

/*****************************************************************************
 *
 *****************************************************************************/
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
unsigned int ppsd_do_stat(struct ppsd_t * ppsd,
                          bool reset_stat,
                          unsigned int pps_nb,
                          unsigned int options) {
    
    if (reset_stat) {
        pps_stats_reset (ppsd->drift_stats, true);
    }
  
    bool print = false;
    if ( (options & PPS_STATS_PRINT_SORTED) 
            && (options & PPS_STATS_PRINT) ) {
        print = true;
        options &= ~((unsigned)PPS_STATS_PRINT);
    }
    pps_stats_header(ppsdout, ppsd->drift_stats, options);
    
    unsigned int pps_cnt = 0u;
    // TODO optional filtering !
    int ok = ppsd_update(ppsd, 0.0l, 0.0l, options);
    while ( (ok >= 0) && (pps_cnt < pps_nb) ) {
        if (ok > 0) {
            pps_cnt ++;
            estimate_set(&ppsd->est, ppsd_timeref(ppsd), ppsd->drift_stats, 0);
        } else {
            // TODO a max nb of outliers to get out
            // TODO if estimate used to filter ?
        }
        ok = ppsd_update(ppsd, 0.0l, 0.0l, options);
    }
    
    if (ok < 0) {
        slogerr("%sReturning after %u/%u PPS\n",
                SLOG_CMT_STR, pps_cnt, pps_nb);
        return pps_cnt;
    }

    if (print) {
        options |= PPS_STATS_PRINT;
        pps_stats_flog(ppsdout, ppsd->drift_stats, options);
    }

    pps_stats_header2(ppsdout, ppsd->drift_stats, options);
    slogout("%s", SLOG_CMT_STR);
    pps_stats_fprint(ppsdout, ppsd->drift_stats, options);

    return pps_cnt;
}

/*****************************************************************************
 *
 *****************************************************************************/
static void _update_adjtimex_fd(void) {
    adjtimex_stdout = ppsdout;
    adjtimex_stderr = ppsderr;
    adjtimex_stddbg = ppsddbg;
}

int ppsd_adj_freq_ppb(struct ppsd_t * ppsd, long ppb) {
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    long freq_ppb = adjtimex_get_freq();
    int ret = adjtimex_adj_freq(ppb);
    ppsd->cum_drift_ppb += ppb;
    adjtimex_snapshot(&ppsd->tx);
    fcmt(ppsdout, "ADJTIMEX adj freq %+ldppb by %+ldppb = %+ldppb return %d\n",
         freq_ppb, ppb, adjtimex_get_freq(), ret);
    return ret;
}

int ppsd_adj_offset_ns(struct ppsd_t * ppsd,
                       long corr_ns,
                       unsigned int options) {
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    int ret = 0;
    if ( (false) && (corr_ns > -1000*1000) && (corr_ns < +1000*1000) ) {
        long acorr_ns = (corr_ns > 0) ? (+corr_ns) : (-corr_ns);
        long max_ppb = 100*1000;
        long s = acorr_ns / max_ppb;
        if (s <= 0) {
            ret = adjtimex_adj_freq(+corr_ns);
            ppsd_update(ppsd, 0.0l, 0.0l, options);
            ret = adjtimex_adj_freq(-corr_ns);
        } else {
            long ppb = (corr_ns > 0) ? (+max_ppb) : (-max_ppb);
            ret = adjtimex_adj_freq(+ppb);
            long k = 0;
            while (k < s) {
                ppsd_update(ppsd, 0.0l, 0.0l, options);
                k ++;
            }
            ret = adjtimex_adj_freq(-ppb);
            ppb = (corr_ns > 0) ? (corr_ns - s*max_ppb)
                                : (corr_ns + s*max_ppb);
            ret = adjtimex_adj_freq(+ppb);
            ppsd_update(ppsd, 0.0l, 0.0l, options);
            ret = adjtimex_adj_freq(-ppb);
        }
    } else {
        ret = adjtimex_set_offset(corr_ns);
    }
    ppsd->cum_off_ns -= corr_ns;
    adjtimex_snapshot(&ppsd->tx);
    fcmt(ppsdout, "ADJTIMEX set offset %+ldns (cum %+lldns) return %d\n",
         corr_ns, ppsd->cum_off_ns, ret);
    return ret;
}

/*****************************************************************************
 *
 *****************************************************************************/
static long long _ppsd_est_offset_ns (struct ppsd_t const * ppsd,
                                      struct timespec const * ts) {
    long long off_ns = estimate_get(&ppsd->est, ts);
    long long stddev_ns = ppsd->est.stddev_ns;
    ass(stddev_ns >= 0);
    if ( (off_ns > -stddev_ns) && (off_ns < +stddev_ns) ) {
        fcmt(ppsdout, "OFFSET |%+lldns| < std dev %lldns, no offset adj.\n",
             off_ns, stddev_ns);
        return 0ll;
    }
    return off_ns;
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_run(struct ppsd_t * ppsd,
             long min_drift_ppb,
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

    int off_nb = pps_stats_max_length(ppsd->off_stats);
    int drift_nb = pps_stats_max_length(ppsd->drift_stats);
    int pps_nb = (off_nb > drift_nb) ? off_nb : drift_nb;
    if (pps_nb <= 0) {
        slogdbg("%s\n", "Continuous stats");
        pps_nb = INT_MAX;
    }

    // TODO optional filtering !
    int pps_cnt = 0u;
    int ok = ppsd_update(ppsd, 0.0l, 0.0l, options);
    while ( (ok >= 0) && (pps_cnt < pps_nb) ) {
        if (ok > 0) {
            pps_cnt ++;
            if ( (off_nb > 0) && (pps_cnt % off_nb == 0) ) {
                // offset
                pps_stats_header2(ppsdout, ppsd->off_stats, options);
                slogout("%s", SLOG_CMT_STR);
                pps_stats_fprint(ppsdout, ppsd->off_stats, options);
                estimate_set(&ppsd->est,
                             ppsd_timeref(ppsd),
                             ppsd->off_stats,
                             0);
                long long corr_ns = -_ppsd_est_offset_ns(ppsd, NULL);
                if ( (corr_ns != 0) 
                        && (-corr_ns > min_offset_ns)
                        && (-corr_ns < max_offset_ns) ) {
                    // set offset and increment
                    ppsd_adj_offset_ns(ppsd, corr_ns, options);
                } else {
                    // TODO TODO TODO
                }
                pps_stats_reset(ppsd->off_stats, true);
            }
            if ( (drift_nb > 0) && (pps_cnt % drift_nb == 0) ) {
                // drift
                pps_stats_header2(ppsdout, ppsd->drift_stats, options);
                slogout("%s", SLOG_CMT_STR);
                pps_stats_fprint(ppsdout, ppsd->drift_stats, options);
                estimate_set(&ppsd->est,
                             ppsd_timeref(ppsd),
                             ppsd->drift_stats,
                             0);
                long long ppb = ppsd->est.drift_ppb;
		long long appb = (ppb > 0) ? (+ppb) : (-ppb);
                if ( (appb > min_drift_ppb) && (appb < max_drift_ppb) ) {
                    ppsd_adj_freq_ppb(ppsd, -ppb);
                    ppsd->cum_drift_ppb -= ppb;
                } else {
                // TODO TODO TODO
                }
                ppsd->cum_off_ns = 0;
                pps_stats_reset(ppsd->drift_stats, true);
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

    return pps_cnt;
}

