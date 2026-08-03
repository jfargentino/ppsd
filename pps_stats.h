#ifndef PPS_STATS_H
#define PPS_STATS_H

#include <stdbool.h>
#include <stdio.h>
#include "timespec_helper.h"

extern FILE* pps_stats_out;
extern FILE* pps_stats_err;
extern FILE* pps_stats_dbg;

//////////////////////////////////////////////////////////////////////////////
#ifndef PPS_STATS_VAR_UNBIASED
    #define PPS_STATS_VAR_UNBIASED 1ul
#endif

/******************************************************************************
 *
 ******************************************************************************/
struct offset_t {
    struct timespec t;
    long long ns;
};

enum pps_stats_print_mask_t {
    PPS_STATS_PRINT_SORTED = 1,
    PPS_STATS_PRINT_ABS_TREF = 2,
    PPS_STATS_PRINT_ABS_TOFF = 4,
    PPS_STATS_PRINT_MEAN = 8,
    PPS_STATS_PRINT_STDDEV = 16,
    PPS_STATS_PRINT_DRIFT = 32,
    PPS_STATS_PRINT_MEDIAN = 64,
    PPS_STATS_PRINT_INFO = 128,
    PPS_STATS_PRINT_UNIT = 256, //(1 << 8)
    PPS_STATS_PRINT = (1u << 16)
};

void offset_fprint(FILE * file,
                   struct offset_t const * x, 
                   unsigned int options);

struct pps_stats_t;

/******************************************************************************
 *
 ******************************************************************************/
struct pps_stats_t * pps_stats_ctor (unsigned int length);

void pps_stats_dtor(struct pps_stats_t * stats);

void pps_stats_reset (struct pps_stats_t * stats, bool zeroing);

void pps_stats_windowed(struct pps_stats_t * stats, unsigned int win_len);

/******************************************************************************
 *
 ******************************************************************************/
unsigned int pps_stats_max_length(struct pps_stats_t const * stats);
unsigned int pps_stats_length(struct pps_stats_t const * stats);

int pps_stats_empty(struct pps_stats_t const * stats);

struct offset_t const * pps_stats_oldest(struct pps_stats_t const * stats);
struct offset_t const * pps_stats_newest(struct pps_stats_t const * stats);
struct offset_t const * pps_stats_lowest(struct pps_stats_t const * stats);
struct offset_t const * pps_stats_highest(struct pps_stats_t const * stats);

struct offset_t const * pps_stats_lower(struct pps_stats_t const * stats,
                                        struct offset_t const * x);

struct offset_t const * pps_stats_older(struct pps_stats_t const * stats,
                                        struct offset_t const * x);

/******************************************************************************
 *
 ******************************************************************************/
unsigned int pps_stats_update (struct pps_stats_t * stats,
                               struct timespec const * t,
                               long long offset_ns);

unsigned int pps_stats_tsupdate (struct pps_stats_t * stats,
                                 struct timespec const * t,
                                 struct timespec const * ts);

void pps_stats_remove_newest(struct pps_stats_t * stats);

/******************************************************************************
 *
 ******************************************************************************/
long double pps_stats_mean(struct pps_stats_t const * stats,
                           long double * stddev,
                           unsigned int window);


long double pps_stats_drift_ppb(struct pps_stats_t const * stats,
                                unsigned int window);

// TODO "windowed" median
long double pps_stats_median (struct pps_stats_t const * stats);

/******************************************************************************
 * IO
 ******************************************************************************/
// print stats header, as specified by options
void pps_stats_header(FILE * file,
                      struct pps_stats_t const * stats,
                      unsigned int options);

// print stats *only* header, as specified by options
void pps_stats_header2(FILE * file,
                       struct pps_stats_t const * stats,
                       unsigned int options);

// print stats results only, as specified by options
void pps_stats_fprint(FILE * file,
                      struct pps_stats_t const * stats,
                      unsigned int options);

// print last timeref, offset and stats, as specified by options
void pps_stats_flast(FILE * file,
                     struct pps_stats_t const * stats,
                     unsigned int options);

// print the whole array of timerefs, offsets and stats, as specified by options
void pps_stats_flog(FILE * file,
                    struct pps_stats_t const * stats,
                    unsigned int options);

// update stats with given string
int pps_stats_sscan(char const * str,
                    size_t str_sz,
                    struct pps_stats_t * stats);

// update stats with one line of the given file
int pps_stats_fscan(FILE * file, struct pps_stats_t * stats);

// update stats with the whole given file
int pps_stats_fread(FILE * file, struct pps_stats_t * stats);

#endif
