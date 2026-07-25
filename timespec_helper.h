#ifndef TIMESPEC_HELPER_H
#define TIMESPEC_HELPER_H

#include <time.h>

//#define TIMESPEC_DBG

// Nb of nanoseconds in one second
#define NS_PER_S (1000L*1000L*1000L)

// Nb of nanoseconds in one second
extern long const ns_per_s;

/*****************************************************************************
 * Normalize the given timespec, that is:
 *     - if tv_sec == 0, -1e9 < tv_nsec < +1e9
 *     - if tv_sec  < 0, -1e9 < tv_nsec <= 0
 *     - if tv_sec  > 0,   0 =< tv_nsec < +1e9
 *****************************************************************************/
void timespec_norm (struct timespec * ts);

/*****************************************************************************
 * tdiff is timespec_norm(t1 - t0)
 *****************************************************************************/
void timespec_diff (struct timespec const * t1,
                    struct timespec const * t0,
                    struct timespec * tdiff);

long long timespec_diff_ns (struct timespec const * t1,
                            struct timespec const * t0);

/*****************************************************************************
 * tsum is timespec_norm(t0 + t1)
 *****************************************************************************/
void timespec_sum (struct timespec const * t0,
                   struct timespec const * t1,
                   struct timespec * tsum);

/*****************************************************************************
 * Returns:
 *     - a negative nb if t0 < t1
 *     - a positive nb if t0 > t1
 *     - 0 if t0 == t1
 *****************************************************************************/
long timespec_cmp (struct timespec const * t0,
                   struct timespec const * t1);

/*****************************************************************************
 * Converts the given timespec in the equivalent nb of nanoseconds.
 *****************************************************************************/
long long timespec2ns (struct timespec const * ts);

/*****************************************************************************
 * returns drift defined as :
 *       (offset - prev_offset) / (ref - prev_ref)
 * For a result in ppb, the numerator is in ns, and denominator in s. 
 * TODO How does it compare with the old formula ?
 *****************************************************************************/
long timespec_drift_ppb (struct timespec const * ts,
                         struct timespec const * prev_ts,
                         struct timespec const * ref,
                         struct timespec const * prev_ref);

#endif //TIMESPEC_HELPER_H

