#ifndef PPSD_H
#define PPSD_H
#include "pps_stats.h"
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

/*****************************************************************************
 *
 *****************************************************************************/
extern FILE * ppsdout;
extern FILE * ppsderr;
extern FILE * ppsddbg;

struct ppsd_t;

/*
 * Open the PPSD sinleton instance.
 *      - path: PPS device (eg "/dev/pps0").
 *      - capture_assert: PPS on rising edge if true, falling edge otherwise.
 *      - hw_offset_ns: PPS offset in ns, if any. Can be negative.
 */
struct ppsd_t * ppsd_open(char const * path,
                          bool capture_assert,
                          long hw_offset_ns);

/* 
 * Set the time reference, i.e. the "absolute real" timestamp of the last PPS.
 * If given timeref is NULL, use its last PPS timestamp rounded to plain
 * second as reference.
 */
time_t ppsd_set_timeref(struct ppsd_t * ppsd,
                        struct timespec const * timeref);

int ppsd_update(struct ppsd_t * ppsd,
                long double predict_ns,
                long double dist2predict_max_ns);

void ppsd_close(struct ppsd_t * ppsd);

/*****************************************************************************
 *
 *****************************************************************************/
struct ppsd_t * ppsd_init(char const * path,
                          bool capture_assert,
                          long hw_offset_ns,
                          unsigned int max_pps_count);

unsigned int ppsd_do_stat(struct ppsd_t * ppsd,
                          bool reset_stat,
                          unsigned int pps_nb,
                          unsigned int options);
 
long long ppsd_est_offset_ns(struct ppsd_t const * ppsd,
                             struct timespec const * ts);

long long ppsd_est_drift_ppb(struct ppsd_t const * ppsd);

long long ppsd_est_stddev_ns (struct ppsd_t const * ppsd);

int ppsd_adj_freq_ppb(struct ppsd_t * ppsd);

int ppsd_set_offset_ns(struct ppsd_t * ppsd);

int ppsd_adj_offset_ns(struct ppsd_t * ppsd,
                       long max_ppb,
                       unsigned int options);

/*****************************************************************************
 *
 *****************************************************************************/
struct timespec const * ppsd_timeref(struct ppsd_t const * ppsd);

struct timespec const * ppsd_timestamp(struct ppsd_t const * ppsd);

long long ppsd_offset_ns(struct ppsd_t const * ppsd);

/*****************************************************************************
 *
 *****************************************************************************/
unsigned long ppsd_stats_init(struct ppsd_t * ppsd,
                              unsigned int max_pps_count);
int ppsd_stats_reset(struct ppsd_t * ppsd);
struct pps_stats_t const * ppsd_stats(struct ppsd_t const * ppsd);
unsigned long ppsd_count(struct ppsd_t const * ppsd);
unsigned long ppsd_outliers(struct ppsd_t const * ppsd);
int ppsd_stats_release(struct ppsd_t * ppsd);

#endif

