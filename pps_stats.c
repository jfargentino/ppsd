#include "pps_stats.h"
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>


FILE* pps_stats_out = NULL;
FILE* pps_stats_err = NULL;
FILE* pps_stats_dbg = NULL;
#define STDOUT pps_stats_out
#define STDERR pps_stats_err
#define STDDBG pps_stats_dbg
#include "slog.h"

/*****************************************************************************
 * Sort-of double linked list, sorted by offset to evaluate the median.
 * Sort-of offset_t sub-class.
 * TODO a outlier boolean ?
 *****************************************************************************/
struct _off_t {
    struct offset_t priv;
    unsigned int prev;
    unsigned int next;
};

/*****************************************************************************
 * Circular offset buffer.
// TODO short window stats too ?
// TODO skewness, kurtosis
// TODO mean - median as poor skewness ?
 *****************************************************************************/
struct pps_stats_t {
    time_t t0; // time reference, to avoid integer overflow
    unsigned int length; // buffer max nb of element
    unsigned int oldest; // index of the oldest offset, next to be wrotten
    unsigned int lowest; // index of the lowest offset
    unsigned int highest; // index of the highest offset
    unsigned int is_full; // increment each time the buffer is full
    unsigned int win_len; // windowed mode
    long double cum; // offset cumulative, for the average 
    long double cum2; // offset^2 cumulative, for the variance
    struct _off_t * offset; // offset buffer
};

/*****************************************************************************
 *
 *****************************************************************************/
void pps_stats_reset (struct pps_stats_t * stats, bool zeroing) {
    slogdbg ("STATS %p reset\n", (void*)stats);
    ass(stats != NULL);
    stats->t0 = 0;
    stats->oldest = 0UL;
    stats->lowest = stats->length;
    stats->highest = stats->length;
    stats->is_full = 0UL;
    stats->cum = 0.0L;
    stats->cum2 = 0.0L;
    if (zeroing) {
        slogdbg ("STATS %p zeroing\n", (void*)stats->offset);
        ass(stats->offset != NULL);
        memset(stats->offset, 0, stats->length*sizeof(struct _off_t));
    }
}

struct pps_stats_t * pps_stats_ctor (unsigned int length) {
    struct pps_stats_t * instance = NULL;
    slogdbg("SZ %zu + %u x %zu = %zu\n",
            sizeof(struct pps_stats_t), length, sizeof(struct _off_t),
            sizeof(struct pps_stats_t) + length*sizeof(struct _off_t));
    instance 
        = malloc (sizeof(struct pps_stats_t) + length*sizeof(struct _off_t));
    ass(instance != NULL);
    unsigned char * ptr
        = ((unsigned char *)instance) + sizeof(struct pps_stats_t);
    ass(ptr >= ((unsigned char*)instance) + sizeof(struct pps_stats_t));
    instance->offset = (struct _off_t *)ptr;
    instance->length = length;
    instance->win_len = 0u;
    slogdbg ("STATS %p of length %u, offset array @%p (%p)\n",
             (void*)instance, length,
             (void*)instance->offset,
             (void*)&(instance->offset));
    pps_stats_reset(instance, true);
    return instance;
}

void pps_stats_windowed(struct pps_stats_t * stats, unsigned int win_len) {
    ass(stats != NULL);
    slogdbg("stats window %u -> %u\n", stats->win_len, win_len);
    stats->win_len = win_len;
}

void pps_stats_dtor(struct pps_stats_t * stats) {
    slogdbg ("STATS %p delete\n", (void*)stats);
    ass(stats != NULL);
    free(stats);
}

/*****************************************************************************
 * Getters
 *****************************************************************************/
unsigned int pps_stats_max_length(struct pps_stats_t const * stats) {
    ass(stats != NULL);
    return stats->length;
}

unsigned int pps_stats_length(struct pps_stats_t const * stats) {
    ass(stats != NULL);
    return (stats->is_full > 0UL) ? stats->length : stats->oldest;
}

int pps_stats_empty(struct pps_stats_t const * stats) {
    return (stats == NULL) ? -1
                           : pps_stats_length(stats) == 0ul ? 1 : 0;
}

static unsigned int _pps_stats_oldest(struct pps_stats_t const * stats) {
    // TODO if none wrotten yet ?
    return (stats->is_full > 0UL) ? stats->oldest : 0u;
}

struct offset_t const * pps_stats_oldest(struct pps_stats_t const * stats) {
    ass(stats != NULL);
    return pps_stats_empty(stats)
                    ? NULL
                    : &(stats->offset[_pps_stats_oldest(stats)].priv);
}

static unsigned int _pps_stats_newest(struct pps_stats_t const * stats) {
    // TODO if none wrotten yet ?
    if (stats->oldest < 1u) {
        unsigned int l = pps_stats_length(stats);
        return (l == 0u) ? 0u : l - 1u;
    }
    return stats->oldest - 1u;
}

struct offset_t const * pps_stats_newest(struct pps_stats_t const * stats) {
    ass(stats != NULL);
    return pps_stats_empty(stats)
                    ? NULL
                    : &(stats->offset[_pps_stats_newest(stats)].priv);
}

struct offset_t const * pps_stats_lowest(struct pps_stats_t const * stats) {
    ass(stats != NULL);
    return pps_stats_empty(stats)
                    ? NULL
                    : &(stats->offset[stats->lowest].priv);
}

struct offset_t const * pps_stats_highest(struct pps_stats_t const * stats) {
    ass(stats != NULL);
    return pps_stats_empty(stats)
                    ? NULL
                    : &(stats->offset[stats->highest].priv);
}


// TODO DANGEROUS !!!
#define _OFF_T(x) ((struct _off_t const *)x)

struct offset_t const * pps_stats_lower(struct pps_stats_t const * stats,
                                        struct offset_t const * x) {
    if (NULL == x) {
        return pps_stats_highest(stats);
    }
    // TODO DANGEROUS !!!
    struct _off_t const * _x = _OFF_T(x);
    if (_x->prev >= pps_stats_length(stats)) {
        return NULL;
    }
    return &(stats->offset[_x->prev].priv);
}

static long _pps_stats_older (struct pps_stats_t const * stats,
                              struct offset_t const * x) {
    // TODO DANGEROUS !!!
    long int k = _OFF_T(x) - stats->offset;
    //slogdbg("stats[k]@%p, stats[0]@%p -> k=%lu\n",
    //        (void const *)x, (void const *)stats->offset, k);
    return k;
}
                                      
struct offset_t const * pps_stats_older(struct pps_stats_t const * stats,
                                        struct offset_t const * x) {
    if (NULL == x) {
        return pps_stats_newest(stats);
    }
    // TODO DANGEROUS !!!
    long k = _pps_stats_older(stats, x);
    if (k == 0) {
        k =  pps_stats_length(stats) - 1u;
    } else {
        k --;
    }
    if (k < 0) {
        // TODO error
        //ass(k > 0);
        return NULL;
    }
    if (k >= pps_stats_length(stats)) {
        // TODO error if >
        ass(k == pps_stats_length(stats));
        return NULL;
    }
    return &(stats->offset[k].priv);
}

/*****************************************************************************
 * Remove an offset from the buffer / double-linked list, that is:
 *     - manage the double linked-list
 *     - update cumulatives
 *****************************************************************************/
static void pps_stats_remove(struct pps_stats_t * stats, unsigned int k) {
    // Remove from the sorted list
    unsigned int prev = stats->offset[k].prev;
    unsigned int next = stats->offset[k].next;
    if (stats->lowest == k) {
        slogdbg("Removing lowest @%u (%+lldns)\n",
                k, stats->offset[k].priv.ns);
        ass(prev == stats->length);
        stats->lowest = next;
        slogdbg("New lowest @%u (%+lldns)\n",
                next, stats->offset[next].priv.ns);
    } else {
        stats->offset[prev].next = next;
    }
    if (stats->highest == k) {
        slogdbg("Removing highest @%u (%+lldns)\n",
                k, stats->offset[k].priv.ns);
        ass(next == stats->length);
        stats->highest = prev;
        slogdbg("New highest @%u (%+lldns)\n",
                prev, stats->offset[prev].priv.ns);
    } else {
        stats->offset[next].prev = prev;
    }
    // remove from cumulatives
    long double y = (long double)stats->offset[k].priv.ns;
    stats->cum -= y;
    stats->cum2 -= y * y;
}

static void pps_stats_remove_oldest(struct pps_stats_t * stats) {
    // TODO _pps_stats_oldest ?
    pps_stats_remove(stats, stats->oldest);
}

void pps_stats_remove_newest(struct pps_stats_t * stats) {
    pps_stats_remove(stats, _pps_stats_newest(stats));
}

/*****************************************************************************
 * Insert an offset in the buffer / double-linked list, that is:
 *     - update cumulatives
 *     - insert the offset at the propoer place in the double linked-list
 *****************************************************************************/
static void pps_stats_insert(struct pps_stats_t * stats,
                             unsigned int k,
                             long long offset_ns) {
    
    stats->offset[k].priv.ns = offset_ns;
    long double xx = (long double)offset_ns;
    stats->cum += xx;
    stats->cum2 += xx * xx;

    if (pps_stats_empty(stats)) {
        ass(stats->lowest == stats->length);
        ass(stats->highest == stats->length);
        stats->lowest = k;
        stats->highest = k;
        stats->offset[k].prev = stats->length;
        stats->offset[k].next = stats->length;
        return;
    }
    
    unsigned int next = stats->lowest;
    unsigned int prev = stats->length;

    while ((next < stats->length)
                 && (offset_ns >= stats->offset[next].priv.ns)) {
        prev = next;
        next = stats->offset[next].next;
    }

    if (next == stats->lowest) {
        // New lowest value
        slogdbg("Insert lowest @%u (%+lldns) instead of @%u (%+lldns)\n",
                k, stats->offset[k].priv.ns,
                next, stats->offset[next].priv.ns);
        ass(prev == stats->length);
        stats->offset[stats->lowest].prev = k;
        stats->offset[k].prev = stats->length;
        stats->offset[k].next = stats->lowest;
        stats->lowest = k;
    } else if (next == stats->length) {
        // New highest value
        slogdbg("Insert highest @%u (%+lldns) instead of @%u (%+lldns)\n",
                k, stats->offset[k].priv.ns,
                prev, stats->offset[prev].priv.ns);
        ass(prev == stats->highest);
        stats->offset[stats->highest].next = k;
        stats->offset[k].prev = stats->highest;
        stats->offset[k].next = stats->length;
        stats->highest = k;
    } else {
        // greater (or equal) than prev but less than next
        stats->offset[prev].next = k;
        stats->offset[k].prev = prev;   
        stats->offset[k].next = next;   
        stats->offset[next].prev = k;
    }
}

/*****************************************************************************
 * Add the new offset value to the circular buffer.
 *****************************************************************************/
unsigned int pps_stats_update (struct pps_stats_t * stats,
                               struct timespec const * t,
                               long long offset_ns) {
    unsigned int k = stats->oldest;
    if (stats->is_full > 0UL) {
        // remove from cum and cum2 the offset which will be overwrotten
        pps_stats_remove_oldest(stats);
    }

    // update the buffer and its cumulators
    if (t != NULL) {
        if (stats->t0 == 0) {
            // TODO beware can be set by sscan to
            slogdbg("T0 %ld -> %ld\n", stats->t0, t->tv_sec);
            stats->t0 = t->tv_sec;
            //slogcmt("STATS T0=%ld\n", stats->t0);
        }
        stats->offset[k].priv.t = *t;
        stats->offset[k].priv.t.tv_sec -= stats->t0;
    } else {
        // TODO stats->t0 = 0 ?
        stats->offset[k].priv.t.tv_sec = 0;
        stats->offset[k].priv.t.tv_nsec = 0;
    }
    pps_stats_insert(stats, k, offset_ns);

    stats->oldest ++;
    if (stats->oldest == stats->length) {
        stats->is_full ++;
        stats->oldest = 0UL;
    }

    return pps_stats_length(stats);
}

/*****************************************************************************
 * Add the new timestamp value to the circular buffer
 *****************************************************************************/
unsigned int pps_stats_tsupdate (struct pps_stats_t * stats,
                                 struct timespec const * t,
                                 struct timespec const * ts) {
    return pps_stats_update (stats, t, timespec_diff_ns(ts, t));
}

/*****************************************************************************
 * Offsets variance and mean.
 *****************************************************************************/
static long double _pps_stats_cums(struct pps_stats_t const * stats,
                                   long double * cum,
                                   unsigned int k) {
    // > thus k == length is a way to test both ways agreed
    if ((k == 0) || (k > pps_stats_length(stats))) {
        *cum = stats->cum;
        return stats->cum2;
    }
    *cum = 0.0L;
    long double cum2 = 0.0L;
    unsigned int n = 0u;
    struct offset_t const * x = pps_stats_older(stats, NULL);
    while (n < k) {
        ass(x != NULL);
        *cum += x->ns;
        cum2 += x->ns * x->ns;
        n ++;
        x = pps_stats_older(stats, x);
    }
    ass(n == k);
    return cum2;
}

static long double pps_stats_var(struct pps_stats_t const * stats,
                                 long double * mean,
                                 unsigned int window) {
    unsigned int length = pps_stats_length(stats);
    if (length == 0u) {
        slogdbg("%s !!!\n", "empty");
        return 0.0l;
    }
    if ((window > 0u) && (window < length)) {
        length = window;
    }
    long double m = 0.0L;
    long double cum2 = _pps_stats_cums(stats, &m, length);
    m /= length;
    if (mean != NULL) {
        *mean = m;
    }
    if (length <= PPS_STATS_VAR_UNBIASED) {
        slogdbg("%s !!!\n", "One only");
        return 0.0L;
    }
    return (cum2 - (long double)length*m*m) 
                 / (long double)(length - PPS_STATS_VAR_UNBIASED);
}

long double pps_stats_mean(struct pps_stats_t const * stats,
                           long double * stddev,
                           unsigned int window) {
    long double mean = 0.0l;
    long double var = pps_stats_var(stats, &mean, window);
    if (stddev != NULL) {
        *stddev = sqrtl(var);
    }
    return mean;
}

/*****************************************************************************
 * Offsets / time covariance.
// TODO one loop only for all ?
 *****************************************************************************/
static long double _pps_stats_tsvar(struct pps_stats_t const * stats,
                                    long double * tsmean,
                                    unsigned int window) {
    unsigned k = window;
    if ((k == 0u) || (k > pps_stats_length(stats))) {
        k = pps_stats_length(stats);
    }
    if (k == 0) {
        slogdbg("%s !!!\n", "empty");
        return 0.0l;
    }

    long double cum = 0.0L;
    long double cum2 = 0.0L;
    unsigned int n = 0u;
    struct offset_t const * x = pps_stats_older(stats, NULL);
    while ((x != NULL) && (n < k)) {
        long double ts_ns = (long double)(x->t.tv_nsec);
        long double ts_s = (long double)(x->t.tv_sec) + ts_ns/1e9L;
        cum += ts_s;
        cum2 += ts_s * ts_s;
        slogdbg("%u: %+.3Lfs -> %+.3Lf & %+.3Lf\n", n, ts_s, cum, cum2);
        n ++;
        x = pps_stats_older(stats, x);
    }
    ass(n == k);

    long double m = cum / k;
    if (tsmean != NULL) {
        *tsmean = m;
    }
    
    if (k <= PPS_STATS_VAR_UNBIASED) {
        slogdbg("%s !!!\n", "One only");
        return 0.0L;
    }
    return (cum2 - (long double)k*m*m) 
                 / (long double)(k - PPS_STATS_VAR_UNBIASED);
}

static long double pps_stats_covar (struct pps_stats_t const * stats,
                                    long double * var_ts,
                                    unsigned int window) {
    unsigned k = window;
    if ((k == 0u) || (k > pps_stats_length(stats))) {
        k = pps_stats_length(stats);
    }
    if (k == 0u) {
        slogdbg("%s !!!\n", "empty");
        return 0.0l;
    }

    long double mean_y = 0.0l;
    // TODO 1st loop
    long double var_y = pps_stats_var(stats, &mean_y, k);
    slogdbg("Offset variance is %Lf, mean %Lf .\n",
            var_y, mean_y);
    long double mean_x = 0.0l;
    // TODO 2nd loop
    long double var_x = _pps_stats_tsvar(stats, &mean_x, k);
    slogdbg("Timeref variance is %Lf, mean %Lf .\n",
            var_x, mean_x);
    if (var_ts != NULL) {
        *var_ts = var_x;
    }

    long double cov = 0.0L;
    unsigned int n = 0u;
    struct offset_t const * x = pps_stats_older(stats, NULL);
    // TODO 3rd loop
    while ((x != NULL) && (n < k)) {
        long double ts_ns = (long double)(x->t.tv_nsec);
        long double ts_s = (long double)(x->t.tv_sec) + ts_ns/1e9L;
        cov += (x->ns - mean_y) * (ts_s - mean_x);
        slogdbg("%u: %+.3Lfns x %+.3Lfs -> %+.3Lf\n",
                n, x->ns - mean_y, ts_s - mean_x, cov);
        n ++;
        x = pps_stats_older(stats, x);
    }
    ass(n == k);
    if (k <= PPS_STATS_VAR_UNBIASED) {
        slogdbg("%s !!!\n", "One only");
        return 0.0L;
    }
    return cov / (long double)(k - (signed)PPS_STATS_VAR_UNBIASED);
}

/*****************************************************************************
 * Offsets drift, ns per s -> ppb
// TODO one loop only for all ?
 *****************************************************************************/
long double pps_stats_drift_ppb (struct pps_stats_t const * stats,
                                 unsigned int window) {
    long double var_ts = 0.0L;
    long double cov_ts = pps_stats_covar (stats, &var_ts, window);
    if ((var_ts > -1e-3L) && (var_ts < +1e-3L)) {
        slogdbg("Timeref variance is %Lf, covariance %Lf .\n",
                var_ts, cov_ts);
        return 0.0L;
    }
    return cov_ts / var_ts;
}

/*****************************************************************************
 *
 *****************************************************************************/
long double pps_stats_median (struct pps_stats_t const * stats) {
    unsigned int len = pps_stats_length(stats);
    if (0u == len) { return 0.0l; }
    unsigned int k = len / 2u;
    struct offset_t const * med = pps_stats_highest(stats);
    while (k > 0) {
        med = pps_stats_lower(stats, med);
        k --;
    }
    if (len % 2) {
        return med->ns;
    }
    struct offset_t const * med2 = pps_stats_lower(stats, med);
    return (med->ns + med2->ns) / 2.0l;
}

/*****************************************************************************
 *
 *****************************************************************************/
void offset_fprint(FILE * file,
                   struct offset_t const * x,
                   unsigned int options) {
    if ( !(options & PPS_STATS_PRINT) ) { return; }
    // Tref
    flog(file, "%4ld", x->t.tv_sec);
    if (x->t.tv_nsec) {
        flog(file, ".%09ld", x->t.tv_nsec);
    }
    flog(file, "%s", ", ");

    // Tpps or offset
    if (options & PPS_STATS_PRINT_ABS_TOFF) {
        struct timespec ts = x->t;
        ts.tv_nsec += x->ns;
        timespec_norm(&ts);
        flog(file, "%ld.%09ld, ", ts.tv_sec, ts.tv_nsec);
    } else {
        flog(file, "%+9lld%s, ", x->ns,
             (options & PPS_STATS_PRINT_UNIT) ? "ns" : "");
    }
}

static void pps_stats_print1(FILE * file,
                             struct pps_stats_t const * stats,
                             unsigned int k,
                             unsigned int options) {
    if ( !(options & PPS_STATS_PRINT) ) { return; }
    if(k >= pps_stats_length(stats)) {
        fcmt(file, "T0 = %ld, ", stats->t0);
        return;
    }

    struct offset_t x = stats->offset[k].priv;
    if (options & PPS_STATS_PRINT_ABS_TREF) {
        x.t.tv_sec += stats->t0;
    }
    offset_fprint(file, &x, options);
}

static void pps_stats_print2(FILE * file,
                             struct pps_stats_t const * stats,
                             unsigned int k,
                             unsigned int options,
                             unsigned int window) {
    
    if ( !(options & PPS_STATS_PRINT) ) { return; }
    bool print_units = (options & PPS_STATS_PRINT_UNIT);

    // stats
    if (options & PPS_STATS_PRINT_MEDIAN) {
        flog(file, "%+.0Lf%s, ",
             pps_stats_median(stats),
             print_units ? "ns" : "");
    }

    if (options & (PPS_STATS_PRINT_MEAN | PPS_STATS_PRINT_STDDEV)) {
        long double mean = 0.0l;
        long double var = 0.0l;
        var = pps_stats_var(stats, &mean, window);
        if (options & PPS_STATS_PRINT_MEAN) {
            flog(file, "%+9.0Lf%s, ", mean, print_units ? "ns" : "");
        }
        if (options & PPS_STATS_PRINT_STDDEV) {
            flog(file, "%9.0Lf%s, ", sqrtl(var), print_units ? "ns" : "");
        }
    }

    if (options & PPS_STATS_PRINT_DRIFT) {
        flog(file, "%+4.0Lf%s, ",
             pps_stats_drift_ppb(stats, window),
             print_units ? "ppb" : "");
    }

    if (options & PPS_STATS_PRINT_INFO) {
        if (options & PPS_STATS_PRINT_SORTED) {
            if (k == _pps_stats_oldest(stats)) {
                flog(file, "%s", " # << OLDEST");
            }
            if (k == _pps_stats_newest(stats)) {
                flog(file, "%s", " # << NEWEST");
            }
        } else {
            if (k == stats->lowest) {
                flog(file, "%s", " # << LOWEST");
            }
            if (k == stats->highest) {
                flog(file, "%s", " # << HIGHEST");
            }
        }
    }

    // endln
    flog(file, "%c", '\n');
}

static void pps_stats_hdr1(FILE * file,
                           struct pps_stats_t const * stats,
                           unsigned int options) {
    if ( !(options & PPS_STATS_PRINT) ) { return; }
    if ( (stats != NULL) 
                &&  (stats->t0 != 0)
                && !(options & PPS_STATS_PRINT_ABS_TREF) )  {
        fcmt(file, "STATS T0=%ld\n", stats->t0);
    }
    flog(file, "%s%s, ",
         SLOG_CMT_STR,
         (options & PPS_STATS_PRINT_ABS_TREF) ? "REF (unix)" : "REF (s)");
    flog(file, "%s, ",
         (options & PPS_STATS_PRINT_ABS_TOFF)
                ? "PPS (unix)" : "PPS offset (ns)");
}

static void pps_stats_hdr2(FILE * file,
                           struct pps_stats_t const * stats,
                           unsigned int options,
                           unsigned int window) {
    if ( !(options & PPS_STATS_PRINT) ) { return; }
    char win_str[32] = {'\0'};
    if ((window > 0) && (window < stats->length)) {
        snprintf(win_str, 32, "%u ", window);
    }

    if (options & PPS_STATS_PRINT_MEDIAN) {
        flog(file, "%s (%s), ", "median", "ns");
    }
    if (options & PPS_STATS_PRINT_MEAN) {
        flog(file, "%s %s(%s), ", "mean", win_str, "ns");
    }
    if (options & PPS_STATS_PRINT_STDDEV) {
        flog(file, "%s %s(%s), ", "std dev", win_str, "ns");
    }
    if (options & PPS_STATS_PRINT_DRIFT) {
        flog(file, "%s %s(%s)", "drift", win_str, "ppb");
    }
}

static void _pps_stats_header(FILE * file,
                              struct pps_stats_t const * stats,
                              unsigned int options,
                              bool hdr1) {
    if ( !(options & PPS_STATS_PRINT) ) { return; }
    if (hdr1) {
        pps_stats_hdr1(file, stats, options);
    } else {
        fcmt(file, "%s", "");
    }
    pps_stats_hdr2(file, stats, options, stats->win_len);
    // endln
    flog(file, "%c", '\n');
}

void pps_stats_header(FILE * file,
                      struct pps_stats_t const * stats,
                      unsigned int options) {
    _pps_stats_header(file, stats, options, true);
}

void pps_stats_header2(FILE * file,
                       struct pps_stats_t const * stats,
                       unsigned int options) {
    _pps_stats_header(file, stats, options, false);
}

void pps_stats_fprint(FILE * file,
                      struct pps_stats_t const * stats,
                      unsigned int options) {
    pps_stats_print2(file, stats, pps_stats_length(stats), options,
                     stats->win_len);
}

static void _pps_stats_kprint(FILE * file,
                              struct pps_stats_t const * stats,
                              unsigned int k,
                              unsigned int options,
                              unsigned int window) {
    pps_stats_print1(file, stats, k, options);
    pps_stats_print2(file, stats, k, options, window);
}

void pps_stats_flast(FILE * file,
                     struct pps_stats_t const * stats,
                     unsigned int options) {
    _pps_stats_kprint(file,
                      stats,
                      _pps_stats_newest(stats),
                      options,
                      stats->win_len);
}

void pps_stats_flog(FILE * file,
                    struct pps_stats_t const * stats,
                    unsigned int options) {
    slogdbg("STATS %p, %u/%u element(s)\n",
            (void const *)stats,
            pps_stats_length(stats),
            stats->length);
    pps_stats_header(file, stats, options);
    bool sorted = (options & PPS_STATS_PRINT_SORTED);
    if (sorted) {
        unsigned int k = stats->lowest;
        while (k < stats->length) {
            _pps_stats_kprint(file, stats, k, options, stats->win_len);
            k = stats->offset[k].next;
        }
    } else {
        for(unsigned int k = stats->oldest; k < pps_stats_length(stats); k++) {
            _pps_stats_kprint(file, stats, k, options, stats->win_len);
        }
        for (unsigned int k = 0u; k < stats->oldest; k++) {
            _pps_stats_kprint(file, stats, k, options, stats->win_len);
        }
    }
}

int pps_stats_sscan(char const * str,
                    size_t str_sz,
                    struct pps_stats_t * stats) {
    
    if (strncmp(str, SLOG_CMT_STR, strlen(SLOG_CMT_STR)) == 0) {
        str += strlen(SLOG_CMT_STR);
        slogdbg("comment \"%s\"\n", str);
        time_t t0 = 0;
        int param_nb = sscanf(str, "STATS T0=%ld", &t0);
        if (param_nb >= 1) {
            slogdbg("T0 %ld -> %ld\n", stats->t0, t0);
            stats->t0 = t0;
            if (stats->t0) {
                //slogcmt("STATS T0=%ld\n", stats->t0);
            }
        }
        return 0;
    }

    struct timespec t = {0};
    struct timespec ts = {0};
    long long ns = 0;
    // TODO relative and absolute for both REF and PPS... ugly
    int item_nb = sscanf(str, "%ld.%09ld, %ld.%09ld, %lld",
                         &t.tv_sec, &t.tv_nsec, &ts.tv_sec, &ts.tv_nsec, &ns);
    slogdbg("\"%s\" %d item(s)\n", str, item_nb);
    if (item_nb == 1) {
        // relative time reference
        item_nb = sscanf(str, "%ld, %ld.%09ld",
                         &t.tv_sec, &ts.tv_sec, &ts.tv_nsec);
        if (item_nb == 2) {
            item_nb = sscanf(str, "%ld, %lld", &t.tv_sec, &ns);
            slogdbg("    - offset %+lldns\n", ns); 
        } else if (item_nb == 3) {
            slogdbg("    - Tpps %ld.%09ld\n", ts.tv_sec, ts.tv_nsec);
            ns = timespec_diff_ns(&ts, &t);
        }
        t.tv_sec += stats->t0;
        slogdbg("    - Tref %ld.%09ld\n", t.tv_sec, t.tv_nsec); 
    } else if (item_nb == 3) {
        item_nb = sscanf(str, "%ld.%09ld, %lld", &t.tv_sec, &t.tv_nsec, &ns);
        slogdbg("    - Tref %ld.%09ld\n", t.tv_sec, t.tv_nsec); 
        slogdbg("    - offset %+lldns\n", ns); 
    } else if (item_nb >= 5) {
        slogdbg("    - Tref %ld.%09ld\n", t.tv_sec, t.tv_nsec); 
        slogdbg("    - Tpps %ld.%09ld\n", ts.tv_sec, ts.tv_nsec); 
        slogdbg("    - offset %+lldns\n", ns); 
    }
    pps_stats_update(stats, &t, ns);
    return item_nb;
}

#include <ctype.h>

int pps_stats_fscan(FILE * file, struct pps_stats_t * stats) {
    #define STR_MAX_SZ 2048
    // static to avoid stack pressure
    static char str[STR_MAX_SZ] = {'\0'};
    if (fgets(str, STR_MAX_SZ, file) == NULL) {
        slogdbg("%s\n", "EOF");
        return -1;
    }
    size_t head = 0;
    while (isspace(str[head])) { head++; }
    size_t tail = strlen(str) - 1;
    while (isspace(str[tail])) { str[tail] = '\0'; tail--; }
    if (str[head] == '\0') {
        return 0;
    }
    return pps_stats_sscan(str + head, tail - head, stats);
}

int pps_stats_fread(FILE * file, struct pps_stats_t * stats) {
    int line_nb = 0;
    int item_nb = pps_stats_fscan(file, stats);
    while (item_nb >= 0) {
        line_nb += (item_nb > 0) ? 1 : 0;
        item_nb = pps_stats_fscan(file, stats);
    }
    return line_nb;
}

/*****************************************************************************
 *
 *****************************************************************************/
#ifdef PPS_STATS_MAIN

#include <errno.h>
#include <string.h>
#include "opt.h"

static struct option long_opts[] = {
    {"count", required_argument, NULL, 'c'},
    {"reset", no_argument, NULL, 'r'},
    {"verbose", no_argument, NULL, 'v'},
    {"windowed", required_argument, NULL, 'w'},
    /* print options */
    {"sorted", no_argument, NULL, 'S'},
    {"abs-tref", no_argument, NULL, 'R'},
    {"abs-toff", no_argument, NULL, 'O'},
    {"median", no_argument, NULL, 'm'},
    {"mean", no_argument, NULL, 'M'},
    {"stddev", no_argument, NULL, 's'},
    {"drift", no_argument, NULL, 'D'},
    {"info", no_argument, NULL, 'I'},
    {"unit", no_argument, NULL, 'U'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    {0,0,0,0}
};

/*
static void _last(FILE * out, unsigned int options,
                  struct pps_stats_t * stats, unsigned int k) {
    unsigned int n = 0u;
    struct offset_t const * x = pps_stats_older(stats, NULL);
    while ((x != NULL) && (n < k)) {
        flog(out, "%u: ", n);
        offset_fprint(out, x, options);
        flog(out, "%c", '\n');
        n ++;
        x = pps_stats_older(stats, x);
    }
}
*/

static int _do_one(FILE * in, FILE * out, unsigned int options,
                   struct pps_stats_t * stats) {
    pps_stats_header(out, stats, options);
    int line_nb = 0;
    int item_nb = pps_stats_fscan(in, stats);
    while (item_nb >= 0) {
        if (item_nb > 0) {
            line_nb ++;
            if (options & PPS_STATS_PRINT_SORTED) {
                if (stats->is_full 
                        && (line_nb % pps_stats_length(stats) == 0)) {
                    pps_stats_flog(out, stats, options);
                }
            } else {
                pps_stats_flast(out, stats, options);
            }
        }
        item_nb = pps_stats_fscan(in, stats);
    }
    if (options & PPS_STATS_PRINT_SORTED) {
    //if ((options & PPS_STATS_PRINT_SORTED) && !(stats->is_full)) {
        pps_stats_flog(out, stats, options);
    }
    //_last(out, options, stats, 8u);
    return line_nb;
}

#include "version.h"

int main (int argc, char ** argv) {
    
    slogcmt("%s %s %s\n\n", argv[0], VERSION, COMMIT_DATE);

    unsigned int options = PPS_STATS_PRINT;
    unsigned int count = 96ul;
    unsigned int window_length = 0u;
    bool reset = false;

    char short_opts[255] = {0};
    longopts2shortopts (long_opts, short_opts);
    int opt;

    pps_stats_out = stdout;
    pps_stats_err = stderr;
    pps_stats_dbg = NULL;

    while ((opt = getopt_long(argc, argv,
                              short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
            case 'S':
            options |= PPS_STATS_PRINT_SORTED;
            break;
            case 'R':
            options |= PPS_STATS_PRINT_ABS_TREF;
            break;
            case 'O':
            options |= PPS_STATS_PRINT_ABS_TOFF;
            break;
            case 'm':
            options |= PPS_STATS_PRINT_MEDIAN;
            break;
            case 'M':
            options |= PPS_STATS_PRINT_MEAN;
            break;
            case 's':
            options |= PPS_STATS_PRINT_STDDEV;
            break;
            case 'D':
            options |= PPS_STATS_PRINT_DRIFT;
            break;
            case 'I':
            options |= PPS_STATS_PRINT_INFO;
            break;
            case 'U':
            options |= PPS_STATS_PRINT_UNIT;
            break;
            case 'c':
            count = atol(optarg);
            break;
            case 'r':
            reset = true;
            break;
            case 'v':
            pps_stats_dbg = pps_stats_err;
            break;
            case 'w':
            window_length = atol(optarg);
            break;
            case 'h':
            default:
            print_usage(argv[0], NULL, long_opts, NULL, NULL);
            exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
        }
    }
    
    struct pps_stats_t * stats = pps_stats_ctor(count);
    pps_stats_windowed(stats, window_length);
    
    if (optind < argc) {
        int line_nb = 0;
        for (int k = optind; k < argc; k++) {
            FILE * pps_file = fopen (argv[k], "rt");
            if (pps_file == NULL) {
                flog(STDERR, "File \"%s\" opening error \"%s\" (%d).\n",
                     argv[k], strerror(errno), errno);
                exit (EXIT_FAILURE);
            }
            line_nb += _do_one(pps_file, STDOUT, options, stats);
            fclose(pps_file);
            fcmt(STDOUT, "file \"%s\" -> %d lines\n", argv[k], line_nb);
            if (reset) {
                pps_stats_reset(stats, true);
                line_nb = 0;
            }
        }
    } else {
        _do_one(stdin, STDOUT, options, stats);
    }

    pps_stats_dtor(stats);
    
    exit (EXIT_SUCCESS);
}

#endif // PPS_STATS_MAIN

