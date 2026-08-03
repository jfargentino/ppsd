#include "ll_stats.h"
#include <stddef.h>

struct ll_stats_t {
    long long * buff;
    unsigned long length;
    unsigned long k;
    unsigned long is_full;
    long long cum;
    unsigned long long cum2;
};

static struct ll_stats_t ll_stats_instance = { 0 };

struct ll_stats_t * ll_stats_init (long long * buff,
                                   unsigned long buff_length) {
    if ((NULL == buff) || (0ul == buff_length)) {
        return NULL;
    }
    ll_stats_instance.buff = buff;
    ll_stats_instance.length = buff_length;
    ll_stats_instance.k = 0ul;
    ll_stats_instance.is_full = 0;
    ll_stats_instance.cum = 0ll;
    ll_stats_instance.cum2 = 0ull;
    return &ll_stats_instance;
}

unsigned long ll_stats_update (struct ll_stats_t * stats, long long x) {
    unsigned long k = stats->k;
    if (stats->is_full > 0ul) {
        // remove cum and cum2 which will be overwrotten
        stats->cum -= stats->buff[k];
        stats->cum2 -= (unsigned)(stats->buff[k] * stats->buff[k]);
    }
    stats->buff[k] = x;
    stats->cum += x;
    stats->cum2 += (unsigned)(x*x);
    stats->k ++;
    if (stats->k == stats->length) {
        stats->is_full ++;
        stats->k = 0ul;
        return stats->length;
    }
    return stats->k;
}

long long ll_stats_mean (struct ll_stats_t const * stats) {
    if ((stats->is_full == 0ul) && (stats->k == 0ul)) {
        // TODO maybe returning LLONG_MAX is better ?
        return 0ll;
    }
    long length = (stats->is_full > 0ul) ? (signed) stats->length
                                         : (signed) stats->k;
    // TODO rounding
    return stats->cum / length;
}

unsigned long long ll_stats_var (struct ll_stats_t const * stats) {
    if ((stats->is_full == 0u) && (stats->k <= LL_STATS_VAR_UNBIASED)) {
        // TODO maybe returning ULLONG_MAX is better ?
        return 0ull;
    }
    /*
    long long mean2 = ll_stats_mean(stats);
    mean2 *= mean2;
    unsigned long length = (stats->is_full > 0ul) ? stats->length
                                                  : stats->k;
    // TODO rounding
    return stats->cum2/(length - LL_STATS_VAR_UNBIASED) - (unsigned)mean2;
    */
    unsigned long length = (stats->is_full > 0ul) ? stats->length
                                                  : stats->k;
    long long m = ll_stats_mean(stats);
    // TODO rounding
    return (stats->cum2 - length*m*m) / (length - LL_STATS_VAR_UNBIASED);
}

long long ll_stats_covar (struct ll_stats_t const * stats) {
    if ((stats->is_full == 0u) && (stats->k <= LL_STATS_VAR_UNBIASED)) {
        // TODO maybe returning ULLONG_MAX is better ?
        return 0ull;
    }
    long long mean_y = ll_stats_mean(stats);
    long length = (stats->is_full > 0ul) ? (signed)stats->length
                                         : (signed)stats->k;
    // x goes from 0 to (length - 1),
    // its mean is length * (length - 1) / 2*length = (length - 1) / 2
    long long cov = 0ll;
    // TODO rounding
    long long mean_k = (length - 1) / 2;
    for (long k = 0; k < length; k ++) {
        cov += (stats->buff[k] - mean_y) * (k - mean_k);
    }
    return cov / (length - (signed)LL_STATS_VAR_UNBIASED);
}

long long ll_stats_drift(struct ll_stats_t const * stats) {
    if ((stats->is_full == 0u) && (stats->k <= LL_STATS_VAR_UNBIASED)) {
        // TODO maybe returning ULLONG_MAX is better ?
        return 0ull;
    }
    long length = (stats->is_full > 0ul) ? (signed)stats->length
                                         : (signed)stats->k;
    long long mean_k = (length - 1) / 2;
    //TODO any optimisation to avoid the loop ?
    long long var_k = 0;
    for (long k = 0; k < length; k ++) {
        var_k += (k - mean_k) * (k - mean_k);
    }
    // TODO rounding
    var_k /= (length - 1);
    return ll_stats_covar (stats) / ((signed)var_k);
}

