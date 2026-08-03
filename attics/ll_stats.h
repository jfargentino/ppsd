#ifndef LL_STATS_H
#define LL_STATS_H

struct ll_stats_t;

struct ll_stats_t * ll_stats_init (long long * buff,
                                   unsigned long buff_length);

unsigned long ll_stats_length (struct ll_stats_t const * stats);

unsigned long ll_stats_update (struct ll_stats_t * stats, long long x);

long double ll_stats_mean (struct ll_stats_t const * stats,
                           unsigned long win_len);

long double ll_stats_dist2mean (struct ll_stats_t const * stats,
                                long long x,
                                unsigned long win_len);

#define LL_STATS_VAR_UNBIASED 1ul

// TODO using "shifted data algorithm" (i.e. var(x-K) == var(x)) to avoid
// TODO the catastrophic cancellation ?
long double ll_stats_var (struct ll_stats_t const * stats,
                          unsigned long win_len);

long double ll_stats_stddev (struct ll_stats_t const * stats,
                             unsigned long win_len,
                             long double * mean);

long double ll_stats_crest (struct ll_stats_t const * stats,
                            long long x,
                            unsigned long win_len,
                            long double * dmean);

long double ll_stats_covar (struct ll_stats_t const * stats);

long double ll_stats_drift_ppb (struct ll_stats_t const * stats);

#endif //LL_STATS_H

