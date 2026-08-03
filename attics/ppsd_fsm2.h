#ifndef PPSD_FSM_H
#define PPSD_FSM_H

#include "adjtimex_helper.h"
#include "ppsd.h"

///////////////////////////////////////////////////////////////////////////////
enum ppsd_state_t {
    PPSD_RELEASED = 0,
    PPSD_INITIALIZED,
    PPSD_LONG_STAT, // not filtered
    PPSD_SHRT_STAT, // filtered
    //PPSD_OFFSET_CORR,
    //PPSD_DRIFT_CORR,
    // ADD STATES BEFORE THIS LINE
    PPSD_STATES_NB
};

// PPS params for init state //////////////////////////////////////////////////
#define PPS_PATH "/dev/pps0"
#define PPS_PATH_SZ 256
#define PPS_ASSERT false
#define PPS_HW_OFF_ns 0
struct param_pps_t {
    char path[PPS_PATH_SZ];
    bool capture_assert;
    long hw_offset_ns;
};

struct param_pps_t * ppsd_param_pps(void);

// stat durations, long and short /////////////////////////////////////////////
struct param_stat_t {
    unsigned int long_s;
    unsigned int shrt_s;
};

struct param_stat_t * ppsd_param_stat(void);

// offset eval/correction parameters //////////////////////////////////////////
struct param_offset_t {
    // if offset < 
    long double min_ns;
    long double max_ns;
    unsigned int corr_s;
    unsigned int corr_max_s;
};

struct param_offset_t * ppsd_param_offset(void);

// drift eval/correction parameters ///////////////////////////////////////////
struct param_drift_t {
    // if |drift| > drift_max_ppb, no correction, probably calculus error
    long double max_ppb;
    // if |drift| < drift_min_ppb, no correction, drift under the noise
    long double min_ppb;
};

struct param_drift_t * ppsd_param_drift(void);

///////////////////////////////////////////////////////////////////////////////
struct ppsd_fsm_t;

struct ppsd_fsm_t * ppsd_fsm(void);

enum ppsd_state_t ppsd_get_state(struct ppsd_fsm_t const * fsm);
enum ppsd_state_t ppsd_run(struct ppsd_fsm_t * fsm, enum ppsd_state_t state);

#endif//PPSD_FSM_H
