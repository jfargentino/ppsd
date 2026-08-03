#include "ll_stats.h"
#include <math.h>
#include <stddef.h>

// TODO update short window stats too ?
struct ll_stats_t {
    long long * buff;
    unsigned long length;
    unsigned long k;
    unsigned long is_full;
    long double cum;
    long double cum2;
};

/*****************************************************************************
 *
 *****************************************************************************/
struct ll_stats_t * ll_stats_init (long long * buff,
                                   unsigned long buff_length) {
    static struct ll_stats_t ll_stats_instance = { 0 };
    if ((NULL == buff) || (0UL == buff_length)) {
        return NULL;
    }
    ll_stats_instance.buff = buff;
    ll_stats_instance.length = buff_length;
    ll_stats_instance.k = 0UL;
    ll_stats_instance.is_full = 0;
    ll_stats_instance.cum = 0.0L;
    ll_stats_instance.cum2 = 0.0L;
    return &ll_stats_instance;
}

/*****************************************************************************
 * TODO update short window stats too ?
 *****************************************************************************/
unsigned long ll_stats_update (struct ll_stats_t * stats, long long x) {
    unsigned long k = stats->k;
    if (stats->is_full > 0UL) {
        // remove from cum and cum2 the offset which will be overwrotten
        long double y = (long double)stats->buff[k];
        stats->cum -= y;
        stats->cum2 -= y * y;
    }
    // update the buffer and its cumulators
    stats->buff[k] = x;
    long double xx = (long double)x;
    stats->cum += xx;
    stats->cum2 += xx * xx;
    stats->k ++;
    if (stats->k == stats->length) {
        stats->is_full ++;
        stats->k = 0UL;
        return stats->length;
    }
    return stats->k;
}

/*****************************************************************************
 *
 *****************************************************************************/
unsigned long ll_stats_length(struct ll_stats_t const * stats) {
    if (NULL == stats) return 0UL;
    return (stats->is_full > 0UL) ? stats->length : stats->k;
}

/*****************************************************************************
 * TODO update short window stats too ?
 * TODO Check sliding stats !!!
 *****************************************************************************/
static long double ll_stats_window (struct ll_stats_t const * stats,
                                    unsigned long win_len,
                                    long double * wmean_ns) {
    
    unsigned long length = ll_stats_length(stats);
    if (length <= LL_STATS_VAR_UNBIASED) {
        return 0.0L;
    }

    long double scum = 0.0l;
    long double scum2 = 0.0l;
    
    if ( (win_len == 0) || (win_len > length) ) {
        // Stats on the whole buffer
        scum = stats->cum;
        scum2 = stats->cum2;
    } else {
        // stats->k is the next index to be written, its the oldest sample
        // newest sample is (stats->k > 0) ? (stats->k - 1) : (length - 1)
        unsigned long k0, k1;
        if (win_len <= stats->k) {
        // win_len <= stats->k is the easy case:
        // stats->k - win_len <= k < stats->k
            k0 = stats->k - win_len;
            k1 = length;
        } else {
        // win_len > stats->k must be done in 2 loops:
        // length - (win_len - stats->k) =< k < length
        // then
        // 0 <= k < stats->k
            k0 = 0u;
            k1 = length - (win_len - stats->k);
        }
        unsigned long k = k0;
        while (k < stats->k) {
            long double xx = stats->buff[k];
            scum += xx;
            scum2 += xx * xx;
            k ++;
        }
        k = k1;
        while (k < length) {
            long double xx = stats->buff[k];
            scum += xx;
            scum2 += xx * xx;
            k ++;
        }
        length = win_len;
    }

    long double m = scum / (long double)length;
    if (wmean_ns != NULL) {
        *wmean_ns = m;
    }
    return (scum2 - (long double)length*m*m) 
                        / (long double)(length - LL_STATS_VAR_UNBIASED);
}

long double ll_stats_mean (struct ll_stats_t const * stats,
                           unsigned long win_len) {
    
    unsigned long length = ll_stats_length(stats);
    if (length <= 0UL) {
        return 0.0L;
    }
    if ( (win_len == 0) || (win_len > stats->length) ) {
        // Return the mean for the whole buffer
        return stats->cum / length;
    }
    long double m = 0.0L;
    ll_stats_window (stats, win_len, &m);
    return m;
}

long double ll_stats_dist2mean (struct ll_stats_t const * stats,
                                long long x,
                                unsigned long win_len) {
    long double m = ll_stats_mean(stats, win_len);
    long double d = (long double)x - m;
    return fabsl(d);
}

#ifndef LL_STATS_VAR_UNBIASED
    #define LL_STATS_VAR_UNBIASED 1ul
#endif

long double ll_stats_var (struct ll_stats_t const * stats,
                          unsigned long win_len) {
    return ll_stats_window (stats, win_len, NULL);
}

long double ll_stats_stddev (struct ll_stats_t const * stats,
                             unsigned long win_len,
                             long double * mean) {
    return sqrtl(ll_stats_window (stats, win_len, mean));
}

long double ll_stats_crest (struct ll_stats_t const * stats,
                            long long x,
                            unsigned long win_len,
                            long double * dmean) {
    long double m = 0.0L;
    long double sd = ll_stats_window (stats, win_len, &m);
    if (sd <= 0.0L) {
        return 0.0L;
    }
    long double d = fabsl(x - m);
    if (dmean != NULL) {
        *dmean = d;
    }
    return d / sd;
}

// TODO windowed version
long double ll_stats_covar (struct ll_stats_t const * stats) {
    unsigned long length = ll_stats_length(stats);
    if (length <= LL_STATS_VAR_UNBIASED) {
        return 0.0L;
    }
    long double mean_y = ll_stats_mean(stats, 0);
    // x goes from 0 to (length - 1),
    // its mean is length * (length - 1) / 2*length = (length - 1) / 2
    // TODO it won't work once some offset filtered out !!!
    long double cov = 0.0L;
    long double mean_k = ((long double)length - 1.0L) / 2.0L;
    for (unsigned long k = 0; k < length; k ++) {
        cov += ((long double)stats->buff[k] - mean_y) 
                                            * ((long double)k - mean_k);
    }
    return cov / (long double)(length - (signed)LL_STATS_VAR_UNBIASED);
}

long double ll_stats_drift_ppb(struct ll_stats_t const * stats) {
    unsigned long length = ll_stats_length(stats);
    if (length <= LL_STATS_VAR_UNBIASED) {
        return 0.0L;
    }
    long double mean_k = ((long double)length - 1.0L) / 2.0L;
    //TODO any optimisation to avoid the loop ?
    long double var_k = 0.0L;
    for (unsigned long k = 0; k < length; k ++) {
        long double ldk = (long double) k;
        var_k += (ldk - mean_k) * (ldk - mean_k);
    }
    // TODO rounding
    var_k /= ((long double)length - 1.0L);
    return ll_stats_covar (stats) / var_k;
}

