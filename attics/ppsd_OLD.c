#include "ppsd.h"
#include "pps_helper.h"
#include "ll_stats.h"
#include "timespec_helper.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************
 *
 *****************************************************************************/
FILE * ppsdout = NULL;
FILE * ppsderr = NULL;

struct ppsd_t {
    struct pps_t * _;
    struct timespec timeref;
    struct timespec timestamp;
    long hw_offset_ns;
    long long * buffer;
    struct ll_stats_t * stats;
    unsigned long count;
    unsigned long outliers;
};

static struct ppsd_t _PPSD_ = {
    ._ = NULL,
    .timeref = { 0 },
    .timestamp = { 0 },
    .hw_offset_ns = 0L,
    .buffer = NULL,
    .stats = NULL,
    .count = 0UL,
    .outliers = 0UL,
};

#define STDOUT ppsdout
#define STDERR ppsderr
#include "slog.h"

struct ppsd_t * ppsd_open(char const * path,
                          bool capture_assert,
                          long hw_offset_ns) {
    pps_stdout = ppsdout;
    pps_stderr = ppsderr;
    _PPSD_._ = pps_open(path, capture_assert);
    if ( _PPSD_._ == NULL) {
        return NULL;
    }
    clock_gettime(CLOCK_REALTIME, &_PPSD_.timeref);
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
int ppsd_kernel_discipline (struct ppsd_t * ppsd) {
    return pps_hardpps(ppsd->_);
}

/*****************************************************************************
 *
 *****************************************************************************/
void ppsd_set_timeref(struct ppsd_t * ppsd,
                      struct timespec const * timeref) {
    if (timeref == NULL) {
        ppsd->timeref = ppsd->timestamp;
        if (ppsd->timeref.tv_nsec >= ns_per_s / 2) {
            ppsd->timeref.tv_sec ++;
        }
        ppsd->timeref.tv_nsec = 0;
    } else {
        ppsd->timeref = *timeref;
        ppsd->timestamp = *timeref;
    }
}

struct timespec const * ppsd_timeref(struct ppsd_t const * ppsd) {
    return &ppsd->timeref;
}

struct timespec const * ppsd_timestamp(struct ppsd_t const * ppsd) {
    return &ppsd->timestamp;
}

struct timespec const * ppsd_offset(struct ppsd_t const * ppsd) {
    static struct timespec offset = { 0 };
    timespec_diff(&ppsd->timestamp, &ppsd->timeref, &offset);
    return &offset;
}

long long ppsd_offset_ns(struct ppsd_t const * ppsd) {
    return timespec2ns(ppsd_offset(ppsd));
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_set_clock(struct ppsd_t * ppsd, long pps_fetch_delay_ns) {
    struct timespec timeset = ppsd->timeref;
    timeset.tv_nsec += pps_fetch_delay_ns;
    timespec_norm(&timeset);
    int ret = pps_set_clock(ppsd->_, &ppsd->timestamp, &timeset);
    if (ret >= 0) {
        // done on timeset by pps_clock_set
        ppsd->timeref.tv_sec ++;
    }
    return ret;
}

/*****************************************************************************
 *
 *****************************************************************************/
#define PPSD_PRINT_PPS_TS true
#define PPSD_PRINT_STATS 16
#define PPSD_PRINT_IDRIFT false

static unsigned long _ppsd_stats_reset(struct ppsd_t * ppsd,
                                       unsigned long max_pps_count,
                                       bool disp_header) {
    ppsd->stats = ll_stats_init(ppsd->buffer, max_pps_count);
    ppsd->count = 0UL;
    ppsd->outliers = 0UL;
    slogdbg("Timestamping %lu PPS edges.\n", max_pps_count);
    if (disp_header) {
        slogcmt("%s", "Tref");
        if (PPSD_PRINT_PPS_TS) {
            slogout("%s", ", Tpps");
        }
        slogout("%s", ", pps offset (ns)");
        if (PPSD_PRINT_STATS > 0) {
            slogout(", mean(%d) (ns), std dev(%d) (ns)",
                    PPSD_PRINT_STATS, PPSD_PRINT_STATS);
        }
        if (PPSD_PRINT_IDRIFT) {
            slogout("%s", ", idrift (ppb)");
        }
        slogout("%c", '\n');
    }
    return max_pps_count;
}

unsigned long ppsd_stats_reset(struct ppsd_t * ppsd,
                               unsigned long max_pps_count) {
    return _ppsd_stats_reset(ppsd, max_pps_count, true);
}

unsigned long ppsd_stats_init(struct ppsd_t * ppsd,
                              unsigned long max_pps_count) {
    if (max_pps_count == 0UL) {
        return 0UL;
    }
    if (ppsd->buffer != NULL) {
        slogout("%s\n", "Stats buffer already allocated !!!");
        return 0UL;
    }
    if (ppsd->stats != NULL) {
        slogout("%s\n", "Stats already initialized !!!");
        return 0UL;
    }
    ppsd->buffer = (long long*)malloc(max_pps_count * sizeof(long long));
    if (NULL == ppsd->buffer) {
        slogout("Can not allocate buffer for offset stats on %luPPS: %s\n",
                max_pps_count,
                strerror(errno));
        return 0UL;
    }
    return _ppsd_stats_reset(ppsd, max_pps_count, false);
}

/*****************************************************************************
 *
 *****************************************************************************/
static int _ppsd_update(struct ppsd_t * ppsd, 
                        long double predict_ns,
                        long double dist2predict_max_ns) {
    // outlier detection
    long long pps_off_ns = ppsd_offset_ns(ppsd);
    long double d2p = fabsl(pps_off_ns - predict_ns);
    if ( (dist2predict_max_ns < 0.0L) || (d2p <= dist2predict_max_ns) ) {
        if (ppsd->stats != NULL) {
            (void)ll_stats_update(ppsd->stats, pps_off_ns);
        }
        ppsd->count ++;
        return 1;
    }
    slogout("%s", SLOG_CMT_STR);
    ppsd->outliers ++;
    return 0;
}

int ppsd_update(struct ppsd_t * ppsd, 
                long double predict_ns,
                long double dist2predict_max_ns) {

    // store previous PPS timestamp
    struct timespec prev_timestamp = ppsd->timestamp; 
    if(prev_timestamp.tv_sec == 0) {
        slogout("%s", SLOG_CMT_STR);
    }
    int ret = pps_get_timestamp(ppsd->_, &ppsd->timestamp, &ppsd->timeref);
    if (ret < 0) {
        slogout("Get PPS error \"%s\" (%d)\n", strerror(errno), errno);
        return -1;
    }
    ppsd->timestamp.tv_nsec -= ppsd->hw_offset_ns;
    timespec_norm(&ppsd->timestamp);
    
    // outlier filtering
    ret = _ppsd_update(ppsd, predict_ns, dist2predict_max_ns);

    // print
    slogout("%ld.%09ld",
            ppsd_timeref(ppsd)->tv_sec,
            ppsd_timeref(ppsd)->tv_nsec);
    if (PPSD_PRINT_PPS_TS) {
        slogout(", %ld.%09ld",
                ppsd_timestamp(ppsd)->tv_sec,
                ppsd_timestamp(ppsd)->tv_nsec);
    }
    slogout(", %+9lld", ppsd_offset_ns(ppsd));
    if (PPSD_PRINT_STATS > 0) {
        long double d = 0.0L;
        long double m = 0.0L;
        if (ppsd->stats != NULL) {
            d = ll_stats_stddev(ppsd->stats, PPSD_PRINT_STATS, &m);
        }
        slogout(", %+.0LF, %.0LF", m, d);
    }
    if (PPSD_PRINT_IDRIFT) {
        struct timespec pps_diff = {0};
        timespec_diff(ppsd_timestamp(ppsd), &prev_timestamp, &pps_diff);
        slogout(", %+lld", timespec2ns(&pps_diff) - ns_per_s);
    }
    slogout("%c",'\n');

    return ret;
}

/*****************************************************************************
 *
 *****************************************************************************/
unsigned long ppsd_count(struct ppsd_t const * ppsd) {
    return ppsd->count;
}

long long ppsd_drift_ppb(struct ppsd_t const * ppsd) {
    if (ppsd->stats == NULL) {
        return 0LL;
    }
    return (long long)(roundl(ll_stats_drift_ppb(ppsd->stats)));
}

long long ppsd_offset_mean_ns(struct ppsd_t const * ppsd,
                              unsigned long win_len) {
    if (ppsd->stats == NULL) {
        return 0LL;
    }
    return (long long)(roundl(ll_stats_mean(ppsd->stats, win_len)));
}

long long ppsd_offset_stddev_ns(struct ppsd_t const * ppsd,
                                unsigned long win_len,
                                long long * off_mean_ns) {
    if (ppsd->stats == NULL) {
        return 0LL;
    }
    long double m = 0.0l;
    long double sd = ll_stats_stddev(ppsd->stats, win_len, &m);
    if (off_mean_ns != NULL) {
        *off_mean_ns = (long long)(roundl(m));
    }
    return (long long)(roundl(sd));
}

void ppsd_stats_print(struct ppsd_t const * ppsd) {
    if (ppsd->count < 3u) { return; }
    slogcmt("%s\n", "count(outliers); mean (ns); std dev (ns); drift (ppb)");
    slogcmt("%lu(%lu); %+lld; %+lld; %+lld\n\n",
            ppsd->count, ppsd->outliers,
            ppsd_offset_mean_ns(ppsd, 0),
            ppsd_offset_stddev_ns(ppsd, 0, NULL),
            ppsd_drift_ppb(ppsd));
}

/*****************************************************************************
 *
 *****************************************************************************/
int ppsd_stats_release(struct ppsd_t * ppsd) {
    if ( (ppsd->buffer == NULL) && (ppsd->stats == NULL) ) {
        return 0;
    }
    int ret = 0;
    if (ppsd->buffer == NULL) {
        slogout("%s\n", "Stats without buffer allocated ?!?");
        ret = -1;
    } else {
        free(ppsd->buffer);
        ppsd->buffer = NULL;
    }
    if (ppsd->stats == NULL) {
        slogout("%s\n", "Buffer allocated without stats ?!?");
        ret = -1;
    } else {
        ppsd->stats = NULL;
    }
    return ret;
}

/*****************************************************************************
 *
 *****************************************************************************/
static volatile sig_atomic_t _do_stats = true;

static void _stop_stats(int signum) {
    slogdbg("Signal %d received while %s.\n",
            signum,
            _do_stats ? "TRUE" : "FALSE");
    switch (signum) {
        case SIGWINCH:
        slogdbg("%s\n", "Ignoring SIGWINCH.");
        break;
        default:
        slogdbg("%s\n", "Stopping.");
        _do_stats = false;
        break;
    }
}

unsigned long ppsd_do_stats(struct ppsd_t * ppsd,
                            unsigned long max_pps_count) {

    struct sigaction new_action = {0};
    struct sigaction old_action = {0};
    new_action.sa_handler = _stop_stats;
    if (sigaction(SIGINT, &new_action, &old_action) < 0) {
        slogout("Can not install signal handler: %s.\n", strerror(errno));
    }
    if (sigaction(SIGWINCH, &new_action, &old_action) < 0) {
        slogout("Can not install signal handler: %s.\n", strerror(errno));
    }

    _ppsd_stats_reset(ppsd, max_pps_count, true);
    unsigned long pps_count = 0UL;
    int pps_ret = ppsd_update(ppsd, 0.0L, -1.0L);
    while ( (pps_ret >= 0) 
                     && (_do_stats)
                     && (pps_count - ppsd->outliers < max_pps_count) )
    {
        pps_count ++;
        pps_ret = ppsd_update(ppsd, 0.0L, -1.0L);
    }

    if (sigaction(SIGINT, &old_action, NULL) < 0) {
        slogout("Can not restore signal handler: %s.\n", strerror(errno));
    }
    if (! _do_stats) {
        slogcmt("Interupted after %lu PPS.\n", pps_count);
    } else if (pps_ret < 0) {
        slogcmt("Timeout after %lu PPS.\n", pps_count);
    } else {
        slogdbg("%lu PPS timestamped.\n", pps_count);
    }
    ppsd_stats_print(ppsd);

    return pps_count;
}

