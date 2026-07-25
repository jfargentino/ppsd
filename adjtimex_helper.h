#ifndef ADJTIMEX_HELPER_H
#define ADJTIMEX_HELPER_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/timex.h>

/******************************************************************************
 *
 ******************************************************************************/
extern FILE* adjtimex_stdout;
extern FILE* adjtimex_stderr;
extern FILE* adjtimex_stddbg;

/******************************************************************************
 * TODO tx.status & STA_PLL and/or STA_FLL to detect skewing ?
 ******************************************************************************/
struct timex_limits_t {
    long tick_min_us;
    long tick_max_us;
    long freq_min_ppb;
    long freq_max_ppb;
    long offset_min_ns;
    long offset_max_ns;
};

struct timex_limits_t const * TIMEX_LIMITS(void);

int timex_cmp(struct timex const * old_tx, struct timex const * new_tx);

void timex_flog(FILE* file, struct timex const * tx);
void adjtimex_log(FILE* file);

int adjtimex_snapshot(struct timex * tx);

/******************************************************************************
 *
 ******************************************************************************/
long adjtimex_get_tick(long * freq_ppb);
int adjtimex_adj_tick(long tick_us);
int adjtimex_set_tick(long tick_us);

long adjtimex_get_freq(void);
int adjtimex_adj_freq(long drift_ppb);
int adjtimex_set_freq(long drift_ppb);

int adjtimex_adj_tick_freq(long tick_us, long freq_ppb);

/******************************************************************************
 * Abruptly set the clock offset.
 * TODO +/-500ms max ?
 ******************************************************************************/
int adjtimex_set_offset(long offset_ns);

/******************************************************************************
 * TODO what is it ???
 ******************************************************************************/
int adjtimex_offset(long offset_ns);

/******************************************************************************
 * Adjust the clock offset by skewing freq.
 * TODO not sure how or if it works... 
 ******************************************************************************/
int adjtimex_singleshot(long offset_ns);
long adjtimex_remaining(void);

/******************************************************************************
 * HOW TODO KERNEL PPS DISCIPLINE ???
 ******************************************************************************/
int adjtimex_hardpps(bool freq, bool phase);

#endif // ADJTIMEX_HELPER_H

