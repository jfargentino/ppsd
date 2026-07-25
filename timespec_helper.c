#include "timespec_helper.h"
#include <math.h>

//#define TIMESPEC_HELPER_DBG
#if (defined TIMESPEC_HELPER_DBG) || (defined TIMESPEC_HELPER_MAIN)
#include <stdio.h>
#define DBG(fmt, ...) (void)fprintf(stderr, \
                                    "[%s:%d] "fmt,\
                                    __FILE__, __LINE__,\
                                    ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

long const ns_per_s = NS_PER_S;

void timespec_norm (struct timespec * ts) {
    DBG("NORM in  %+ld.%+09ld\n", ts->tv_sec, ts->tv_nsec);
    while (ts->tv_nsec <= -ns_per_s) {
        ts->tv_nsec += ns_per_s;
        ts->tv_sec --;
    }
    while (ts->tv_nsec >= +ns_per_s) {
       ts->tv_nsec -= ns_per_s;
       ts->tv_sec ++;
    }
    if (ts->tv_sec < 0) {
        while (ts->tv_nsec > 0) {
            ts->tv_nsec -= ns_per_s;
            ts->tv_sec ++;
        }
    } else if (ts->tv_sec > 0) {
        while (ts->tv_nsec < 0) {
            ts->tv_nsec += ns_per_s;
            ts->tv_sec --;
        }
    }
    if (ts->tv_sec == 0) {
        DBG("NORM out %+ldns\n", ts->tv_nsec);
    } else {
        DBG("NORM out %+ld.%+010ld\n", ts->tv_sec, ts->tv_nsec);
    }
}

long long timespec_diff_ns (struct timespec const * t1,
                            struct timespec const * t0) {
    long s = t1->tv_sec - t0->tv_sec;
    long long ns = s*1000*1000*1000 + t1->tv_nsec - t0->tv_nsec;
    return ns;
}

void timespec_diff (struct timespec const * t1,
                    struct timespec const * t0,
                    struct timespec * tdiff) {
    tdiff->tv_sec = t1->tv_sec - t0->tv_sec;
    tdiff->tv_nsec = t1->tv_nsec - t0->tv_nsec;
    timespec_norm (tdiff);
    if (tdiff->tv_sec == 0) {
        DBG("DIFF %+ld.%09ld - %+ld.%09ld = %+ldns\n",
            t1->tv_sec, t1->tv_nsec,
            t0->tv_sec, t0->tv_nsec,
            tdiff->tv_nsec);
    } else {
        DBG("DIFF %+ld.%09ld - %+ld.%09ld = %+ld.%09ld\n",
            t1->tv_sec, t1->tv_nsec,
            t0->tv_sec, t0->tv_nsec,
            tdiff->tv_sec, tdiff->tv_nsec);
    }
}

void timespec_sum (struct timespec const * t0,
                   struct timespec const * t1,
                   struct timespec * tsum) {
    tsum->tv_sec = t0->tv_sec + t1->tv_sec;
    tsum->tv_nsec = t0->tv_nsec + t1->tv_nsec;
    timespec_norm (tsum);
    if (tsum->tv_sec == 0) {
        DBG("ADD  %+ld.%09ld + %+ld.%09ld = %+ldns\n",
            t0->tv_sec, t0->tv_nsec,
            t1->tv_sec, t1->tv_nsec,
            tsum->tv_nsec);
    } else {
        DBG("ADD  %+ld.%09ld + %+ld.%09ld = %+ld.%09ld\n",
            t0->tv_sec, t0->tv_nsec,
            t1->tv_sec, t1->tv_nsec,
            tsum->tv_sec, tsum->tv_nsec);
    }
}

long long timespec2ns (struct timespec const * ts) {
    return ((long long)(ts->tv_sec) * ns_per_s) + ts->tv_nsec;
}

long timespec_cmp (struct timespec const * t0,
                   struct timespec const * t1) {
    long cmp;
    if (t0->tv_sec == t1->tv_sec) {
        cmp = t0->tv_nsec - t1->tv_nsec;
    } else {
        cmp = t0->tv_sec - t1->tv_sec;
    }
    DBG("CMP  %ld.%09ld with %ld.%09ld = %+ld\n",
        t0->tv_sec, t0->tv_nsec,
        t1->tv_sec, t1->tv_nsec,
        cmp);
    return cmp;
}

/*****************************************************************************
 * returns drift defined as :
 *       (ts - prev_ts - (ref - prev_ref)) / (ref - prev_ref)
 * For a result in ppb, the numerator is in ns, and denominator in s. 
 *****************************************************************************/
/*
long timespec_drift_ppb (struct timespec const * ts,
                         struct timespec const * prev_ts,
                         struct timespec const * ref,
                         struct timespec const * prev_ref) {
    DBG("DRIFT %s\n", "*****************************");
    struct timespec ts_diff;
    timespec_diff(ts, prev_ts, &ts_diff);
    struct timespec ref_diff;
    timespec_diff(ref, prev_ref, &ref_diff);
    long long ts_diff_ns = timespec2ns (&ts_diff);
    long long ref_diff_ns = timespec2ns (&ref_diff);
    DBG("DRIFT actual=%lldns vs ref=%lldns\n",
        ts_diff_ns, ref_diff_ns);
    ts_diff_ns -= ref_diff_ns;
    long double ref_diff_s = (long double)ref_diff_ns / 1e9L;
    long ppb = (long)(roundl((long double)ts_diff_ns / ref_diff_s));
    DBG("DRIFT %lldns in %.9Lfs = %+ldppb\n",
        ts_diff_ns, ref_diff_s, ppb);
    return ppb;
}
*/

long timespec_drift_ppb (struct timespec const * offset,
                         struct timespec const * prev_offset,
                         struct timespec const * ref,
                         struct timespec const * prev_ref) {
    DBG("DRIFT %s\n", "*****************************");
    struct timespec offset_diff;
    timespec_diff(offset, prev_offset, &offset_diff);
    struct timespec ref_diff;
    timespec_diff(ref, prev_ref, &ref_diff);
    long long offset_diff_ns = timespec2ns (&offset_diff);
    long long ref_diff_ns = timespec2ns (&ref_diff);
    long double ref_diff_s = (long double)ref_diff_ns / 1e9L;
    long ppb = (long)(roundl((long double)offset_diff_ns / ref_diff_s));
    DBG("DRIFT from %+lldns to %+lldns (%+lldns) in %.9Lfs = %+ldppb\n",
        timespec2ns(prev_offset), timespec2ns(offset), offset_diff_ns,
        ref_diff_s, ppb);
    return ppb;
}

#ifdef TIMESPEC_HELPER_MAIN

#ifdef NDEBUG
#warning "NDEBUG is defined !!!"
#endif
#include <assert.h>

int main (void) {

    struct timespec const zero = { .tv_sec = 0, .tv_nsec = 0 };
    assert(timespec2ns(&zero) == 0);
    assert(timespec_cmp(&zero, &zero) == 0);
    
    struct timespec const one = { .tv_sec = 1, .tv_nsec = 0 };
    assert(timespec2ns(&one) == ns_per_s);
    assert(timespec_cmp(&one, &one) == 0);
    
    assert(timespec_cmp(&one, &zero) > 0);
    assert(timespec_cmp(&zero, &one) < 0);

    struct timespec t0 = { 0 };
    struct timespec t1 = { 0 };
    struct timespec t2 = { 0 };
    
    t0.tv_sec = 0;
    t0.tv_nsec = ns_per_s;
    timespec_norm(&t0);
    assert(t0.tv_sec == 1);
    assert(t0.tv_nsec == 0);
    assert(timespec_cmp(&t0, &one) == 0);
    timespec_diff(&t0, &one, &t1);
    assert(t1.tv_sec == 0);
    assert(t1.tv_nsec == 0);
    assert(timespec_cmp(&t1, &zero) == 0);
    
/*
    t1 = t0;

    t0.tv_sec = 0;
    t0.tv_nsec = ns_per_s - 1;
    t1 = t0;
    timespec_norm(&t1);

    t0.tv_sec = 0;
    t0.tv_nsec = ns_per_s + 1;
    t1 = t0;
    timespec_norm(&t1);

    t0.tv_sec = 0;
    t0.tv_nsec = -ns_per_s;
    t1 = t0;
    timespec_norm(&t1);

    t0.tv_sec = 0;
    t0.tv_nsec = 1 - ns_per_s;
    t1 = t0;
    timespec_norm(&t1);

    t0.tv_sec = 0;
    t0.tv_nsec = ns_per_s + 1;
    t1.tv_sec = 0;
    t1.tv_nsec = ns_per_s;
    timespec_diff(&t0, &t1, &ts);
    timespec_diff(&t1, &t0, &ts);

    t0.tv_sec = 0;
    t0.tv_nsec = ns_per_s + 1;
    timespec_norm(&t0);
    t1.tv_sec = 0;
    t1.tv_nsec = ns_per_s;
    timespec_norm(&t1);
    timespec_diff(&t0, &t1, &ts);
    timespec_diff(&t1, &t0, &ts);

    t0.tv_sec = 1;
    t0.tv_nsec = 1;
    t1.tv_sec = 1;
    t1.tv_nsec = 0;
    timespec_diff(&t0, &t1, &ts);
    timespec_diff(&t1, &t0, &ts);

    t0.tv_sec = 1;
    t0.tv_nsec = +1;
    t1.tv_sec = 1;
    t1.tv_nsec = -1;
    timespec_diff(&t0, &t1, &ts);
    timespec_diff(&t1, &t0, &ts);
    timespec_sum(&t0, &t1, &ts);

    t0.tv_sec = 1;
    t0.tv_nsec = +1;
    t1.tv_sec = 1;
    t1.tv_nsec = -1;
    timespec_norm(&t1);
    timespec_diff(&t0, &t1, &ts);
    timespec_diff(&t1, &t0, &ts);
    timespec_sum(&t0, &t1, &ts);

    t0.tv_sec = 1;
    t0.tv_nsec = +1;
    t1.tv_sec = -1;
    t1.tv_nsec = -2;
    timespec_sum(&t0, &t1, &ts);

    t0.tv_sec = 0;
    t0.tv_nsec = ns_per_s - 1;
    t1.tv_sec = 1;
    t1.tv_nsec = 1;
    timespec_sum(&t0, &t1, &ts);

    t0.tv_sec = 0;
    t0.tv_nsec = 0;
    t1.tv_sec = 0;
    t1.tv_nsec = ns_per_s - 1;
    timespec_diff(&t0, &t1, &ts);
    timespec_sum(&t1, &ts, &t0);

    t0.tv_sec = 0;
    t0.tv_nsec = 0;
    t1.tv_sec = 0;
    t1.tv_nsec = ns_per_s + 1;
    timespec_diff(&t0, &t1, &ts);
    timespec_sum(&t1, &ts, &t0);
*/

    return 0;
}
#endif // TIMESPEC_HELPER_MAIN
