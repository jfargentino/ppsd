#ifndef PPS_HELPER_H
#define PPS_HELPER_H
#include <stdbool.h>
#include <time.h>

#include <stdio.h>
extern FILE* pps_stdout;
extern FILE* pps_stderr;
extern FILE* pps_stddbg;

struct pps_t;

/*****************************************************************************
 * The PPS edge is a global system parameter ! Probably set the interrupt...
 *****************************************************************************/
struct pps_t * pps_open(char const * path, bool capture_assert);

/*****************************************************************************
 *
 *****************************************************************************/
void pps_close(struct pps_t * pps);

/*****************************************************************************
 *
 *****************************************************************************/
int pps_get_timestamp(struct pps_t const * pps,
                      struct timespec * pps_timestamp);

/*****************************************************************************
 *
 *****************************************************************************/
int pps_set_clock(struct pps_t const * pps,
                  struct timespec * pps_timestamp,
                  struct timespec const * timeref);

/*****************************************************************************
 * TODO
 *****************************************************************************/
int pps_hardpps(struct pps_t const * pps);

#endif

