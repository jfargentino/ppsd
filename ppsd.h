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
 * Open the PPSD singleton instance and its statistics for drift/offset
 *      - path: PPS device (eg "/dev/pps0").
 *      - capture_assert: PPS on rising edge if true, falling edge otherwise.
 *      - hw_offset_ns: PPS offset in ns, if any. Can be negative.
 */
struct ppsd_t * ppsd_open(char const * path,
                          bool capture_assert,
                          long hw_offset_ns,
                          unsigned int drift_pps_count,
                          unsigned int offset_pps_count);

/* 
 * Set the time reference, i.e. the "absolute real" timestamp of the last PPS.
 * If given timeref is NULL, use its last PPS timestamp rounded to plain
 * second as reference.
 */
time_t ppsd_set_timeref(struct ppsd_t * ppsd,
                        struct timespec const * timeref);

/*
 * Wait for a PPS, add it to statistics if offset is near prediction.
 * If given max distance is <= 0, no fitering.
 */
int ppsd_update(struct ppsd_t * ppsd,
                long double predict_ns,
                long double dist2predict_max_ns,
                unsigned int options);

void ppsd_close(struct ppsd_t * ppsd);

/*
 * Adjust the CLK frequency with the last estimated drift.
 */
int ppsd_adj_drift_ppb(struct ppsd_t * ppsd, long max_drift_ppb);

/*
 * Abruptly set the CLK to account the last estimated offset.
 * TODO Temporary adjust the CLK freq to account the last estimated offset.
 *
 *   -500ms       0       +500ms
 * ----|======++++|++++======|----
 * ----|----------|----------|----
 * ----|==========|==========|----
 * ----|======++++|++++++++++|----
 */
int ppsd_adj_offset_ns(struct ppsd_t * ppsd,
                       long min_offset_ns,
                       long max_offset_ns,
                       unsigned int options);

int ppsd_run(struct ppsd_t * ppsd,
             long max_drift_ppb,
             long min_offset_ns,
             long max_offset_ns);

#endif

