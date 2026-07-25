#include "ppsd.h"
#include "pps_helper.h"
#include "adjtimex_helper.h"
#include "timespec_helper.h"
#include <errno.h>
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
                          struct ppsd_t const * ppsd,
                          unsigned int win) {
    est->ts = *ppsd_timeref(ppsd);
    long double drift_ppb = pps_stats_drift_ppb(ppsd_stats(ppsd), win);
    long double offset_ns = 0.0l;
    long double var = pps_stats_var(ppsd_stats(ppsd),
                                    &offset_ns,
                                    win);
    // Offset estimate is not the mean !
    if (win <= 0) {
        win = pps_stats_length(ppsd_stats(ppsd));
    }
    offset_ns += (win/2 + 1) * drift_ppb;
    est->offset_ns = roundl(offset_ns);
    est->drift_ppb = roundl(drift_ppb);
    est->stddev_ns = roundl(sqrtl(var));
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

static long long estimate_stddev (struct estimate_t const * est) {
    return est->stddev_ns;
}

/*****************************************************************************
 *
 *****************************************************************************/
struct ppsd_t {
    struct pps_t * _;
    struct timespec timeref;
    struct timespec timestamp;
    long hw_offset_ns;
    struct pps_stats_t * stats;
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
    .stats = NULL,
    .count = 0UL,
    .outliers = 0UL,
    .est = { { 0 } },
    .tx = { 0 }
};

/*****************************************************************************
 *
 *****************************************************************************/
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
struct ppsd_t * ppsd_open(char const * path,
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

struct timespec const * ppsd_timeref(struct ppsd_t const * ppsd) {
    return &ppsd->timeref;
}

struct timespec const * ppsd_timestamp(struct ppsd_t const * ppsd) {
    return &ppsd->timestamp;
}

long long ppsd_offset_ns(struct ppsd_t const * ppsd) {
    return timespec_diff_ns(&ppsd->timestamp, &ppsd->timeref);
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_stats_reset(struct ppsd_t * ppsd) {
    ppsd->count = 0UL;
    ppsd->outliers = 0UL;
    ppsd_set_timeref(ppsd, NULL);
    if (ppsd->stats != NULL) {
        pps_stats_reset (ppsd->stats, true);
        return 1;
    }
    return 0;
}

unsigned long ppsd_stats_init(struct ppsd_t * ppsd,
                              unsigned int max_pps_count) {
    if (ppsd->stats != NULL) {
        slogout("%s\n", "Stats already initialized !!!");
        return 0ul;
    }
    ppsd->stats = pps_stats_ctor(max_pps_count);
    ppsd_stats_reset(ppsd);
    return max_pps_count;
}

struct pps_stats_t const * ppsd_stats(struct ppsd_t const * ppsd) {
    return ppsd->stats;
}

int ppsd_stats_release(struct ppsd_t * ppsd) {
    if (ppsd->stats == NULL) {
        slogerr("%s\n", "No stats to release");
        return 0;
    }
    pps_stats_dtor(ppsd->stats);
    ppsd->stats = NULL;
    return 1;
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_update(struct ppsd_t * ppsd, 
                long double predict_ns,
                long double dist2predict_max_ns) {

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
    if (outlier) {
        ppsd->outliers ++;
        slogdbg("outlier #%lu: offset %+lldns, estimate %+.0Lfns +/-%.0Lfns\n",
                ppsd->outliers, pps_off_ns, predict_ns, dist2predict_max_ns);
    } else {
        ppsd->count ++;
    }
    if ((ppsd->stats != NULL) && !outlier) {
        (void)pps_stats_update(ppsd->stats, ppsd_timeref(ppsd), pps_off_ns);
    }
    
    return outlier ? 0 : 1;
}

/*****************************************************************************
 *
 *****************************************************************************/
unsigned long ppsd_count(struct ppsd_t const * ppsd) {
    return ppsd->count;
}

unsigned long ppsd_outliers(struct ppsd_t const * ppsd) {
    return ppsd->outliers;
}

/*****************************************************************************
 *
 *****************************************************************************/
struct ppsd_t * ppsd_init(char const * path,
                          bool capture_assert,
                          long hw_offset_ns,
                          unsigned int max_pps_count) {
    struct ppsd_t * ppsd = ppsd_open(path, capture_assert, hw_offset_ns);
    if (NULL == ppsd) {
        return NULL;
    }
    adjtimex_snapshot(&ppsd->tx);
    fcmt(ppsderr, "%s\n", "ADJTIMEX at start:");
    timex_flog(ppsderr, &ppsd->tx);
    ppsd_stats_init(ppsd, max_pps_count); // TODO check return
    return ppsd;
}

static int ppsd_update_out(struct ppsd_t * ppsd, 
                           long double predict_ns,
                           long double dist2predict_max_ns,
                           unsigned int options) {
    int ok = ppsd_update(ppsd, predict_ns, dist2predict_max_ns);
    if (ok > 0) {
        pps_stats_flast(ppsdout, ppsd_stats(ppsd), options);
    } else if (ok == 0) {
        // outlier
        fcmt(ppsdout, "%ld, %+9lld\n",
             ppsd_timeref(ppsd)->tv_sec,
             ppsd_offset_ns(ppsd));
    }
    return ok;
}

unsigned int ppsd_do_stat(struct ppsd_t * ppsd,
                          bool reset_stat,
                          unsigned int pps_nb,
                          unsigned int options) {
    
    if (reset_stat) {
        ppsd_stats_reset(ppsd);
    }
  
    bool print = false;
    if ( (options & PPS_STATS_PRINT_SORTED) 
            && (options & PPS_STATS_PRINT) ) {
        print = true;
        options &= ~((unsigned)PPS_STATS_PRINT);
    }
    pps_stats_header(ppsdout, ppsd_stats(ppsd), options);
    
    unsigned int pps_cnt = 0u;
    // TODO optional filtering !
    int ok = ppsd_update_out(ppsd, 0.0l, 0.0l, options);
    while ( (ok >= 0) && (pps_cnt < pps_nb) ) {
        if (ok > 0) {
            pps_cnt ++;
            estimate_set(&ppsd->est, ppsd, 0);
        } else {
            // TODO a max nb of outliers to get out
            // TODO if estimate used to filter ?
        }
        ok = ppsd_update_out(ppsd, 0.0l, 0.0l, options);
    }
    
    if (ok < 0) {
        slogerr("%sReturning after %u/%u PPS\n",
                SLOG_CMT_STR, pps_cnt, pps_nb);
        return pps_cnt;
    }

    if (print) {
        options |= PPS_STATS_PRINT;
        pps_stats_flog(ppsdout, ppsd_stats(ppsd), options);
    }

    pps_stats_header2(ppsdout, ppsd_stats(ppsd), options);
    slogout("%s", SLOG_CMT_STR);
    pps_stats_fprint(ppsdout, ppsd_stats(ppsd), options);

    return pps_cnt;
}

/*****************************************************************************
 *
 *****************************************************************************/
long long ppsd_est_offset_ns (struct ppsd_t const * ppsd,
                              struct timespec const * ts) {
    return estimate_get(&ppsd->est, ts);
}

long long ppsd_est_drift_ppb (struct ppsd_t const * ppsd) {
    return ppsd->est.drift_ppb;
}

long long ppsd_est_stddev_ns (struct ppsd_t const * ppsd) {
    return estimate_stddev(&ppsd->est);
}

/*****************************************************************************
 *
 *****************************************************************************/
static void _update_adjtimex_fd(void) {
    adjtimex_stdout = ppsdout;
    adjtimex_stderr = ppsderr;
    adjtimex_stddbg = ppsddbg;
}

int ppsd_adj_freq_ppb(struct ppsd_t * ppsd) {
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    long freq_ppb = adjtimex_get_freq();
    // TODO if freq_ppb < stddev / nb_pps ?
    int ret = adjtimex_adj_freq(-ppsd_est_drift_ppb(ppsd));
    adjtimex_snapshot(&ppsd->tx);
    fcmt(ppsdout, "ADJTIMEX adj freq %+ldppb by %+lldppb = %+ldppb return %d\n",
         freq_ppb, -ppsd_est_drift_ppb(ppsd), adjtimex_get_freq(), ret);
    return ret;
}

static long long _ppsd_est_offset_ns (struct ppsd_t const * ppsd,
                                      struct timespec const * ts) {
    long long off_ns = ppsd_est_offset_ns(ppsd, ts);
    long long stddev_ns = ppsd_est_stddev_ns(ppsd);
    if ( (off_ns > -stddev_ns) && (off_ns < +stddev_ns) ) {
        fcmt(ppsdout, "OFFSET |%+lldns| < std dev %lldns, no offset adj.\n",
             off_ns, stddev_ns);
        return 0ll;
    }
    return off_ns;
}

int ppsd_set_offset_ns(struct ppsd_t * ppsd) {
    long long corr_ns = -_ppsd_est_offset_ns(ppsd, NULL);
    if ( true && (corr_ns  == 0ll) ) {
        return 0;
    }
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    int ret = adjtimex_set_offset(corr_ns);
    adjtimex_snapshot(&ppsd->tx);
    fcmt(ppsdout, "ADJTIMEX set offset %+lldns return %d\n",
         corr_ns, ret);
    return ret;
}

int ppsd_adj_offset_ns(struct ppsd_t * ppsd,
                       long max_ppb,
                       unsigned int options) {
    long long ppb = _ppsd_est_offset_ns(ppsd, NULL);
    if ( true && (ppb == 0ll) ) {
        return 0;
    }
    _update_adjtimex_fd();
    if (adjtimex_snapshot(&ppsd->tx) > 0) {
        fcmt(ppsdout, "%s\n", "ADJTIMEX change detected:");
        timex_flog(ppsdout, &ppsd->tx);
    }
    // TODO in 1s or more !
    long appb = (ppb > 0l) ? (+ppb) : (-ppb);
    long s = appb / max_ppb;
    if (s == 0) {
        // only 1s using offset_ns for freq adj
        s = 1;
    } else {
        ppb = (ppb > 0l) ? (+max_ppb) : (-max_ppb);
        // TODO remaining appb % max_ppb 
    }
    long freq_ppb = adjtimex_get_freq();
    int ret = adjtimex_adj_freq(-ppb);
    fcmt(ppsdout,
         "ADJTIMEX freq %+ldppb %+lldppb for %lds = %+ldppb return %d\n",
         freq_ppb, -ppb, s, adjtimex_get_freq(), ret);
    while (s > 0) {
        ppsd_update_out(ppsd, 0.0l, 0.0l, options);
        s --;
    }
    ret = adjtimex_adj_freq(+ppb);
    adjtimex_snapshot(&ppsd->tx);
    return ret;
}

