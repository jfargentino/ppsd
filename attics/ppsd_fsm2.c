/*****************************************************************************
 *
 *****************************************************************************/
#include "ppsd_fsm.h"
#include <math.h>

#define STDOUT ppsdout
#define STDERR ppsderr
#define STDDBG ppsddbg
#include "slog.h"

/*****************************************************************************
 *
 *****************************************************************************/
struct estimate_t {
    struct timespec ts;
    long double offset_ns;
    long double drift_ppb;
    long double stddev_ns;
};

static long double estimate_next (struct estimate_t const * est) {
    return est->offset_ns + est->drift_ppb;
}

// TODO 3.0 is probably too low
#define OUTLIERS_FACTOR 3.0L

// TODO not sufficient -> probably we need to sort out right side of the
// gaussian bell only, when offset is way over (very late)
static long double estimate_max_dev (struct estimate_t const * est) {
    return OUTLIERS_FACTOR * est->stddev_ns;
}

static void estimate_set (struct estimate_t * est,
                          struct ppsd_t const * ppsd,
                          unsigned int win) {
    est->ts = *ppsd_timeref(ppsd);
    est->drift_ppb = pps_stats_drift_ppb(ppsd_stats(ppsd), win);
    est->stddev_ns = pps_stats_var(ppsd_stats(ppsd),
                                   &est->offset_ns,
                                   win);
    // Offset estimate is not the mean !
    if (win <= 0) {
        win = pps_stats_length(ppsd_stats(ppsd));
    }
    est->offset_ns += (win/2 + 1) * est->drift_ppb;
    est->offset_ns = roundl(est->offset_ns);
    est->drift_ppb = roundl(est->drift_ppb);
    est->stddev_ns = roundl(sqrtl(est->stddev_ns));
}

/*****************************************************************************
 *
 *****************************************************************************/
// init -> short offset eval, if offset > 1ms(?) coarse settings
// drift eval (long stats without filtering) -> drift corr (instantaneous)
// offset eval (short stats with filtering) -> offset corr (0s if coarse, 1s +)
// if |drift| >/< DRIFT_MIN/DRIFT_MAX, drift correction
// if offset >/< OFFSET_COARSE_MAX/OFFSET_COARSE_MIN coarse offset correction
// if |offset| >/< OFFSET_MIN/OFFSET_MAX offset correction
struct ppsd_fsm_t {
    struct timex tx;
    unsigned long state_s;
    enum ppsd_state_t state;
    struct ppsd_t * ppsd;
    struct estimate_t est;
    struct param_pps_t p_pps;
    struct param_stat_t p_stat;
    struct param_offset_t p_off;
    struct param_drift_t p_drift;
    unsigned int stats_options;
};

struct ppsd_fsm_t * ppsd_fsm(void) {
    static struct ppsd_fsm_t _FSM_ = {
        //.tx = { 0 },
        .state = PPSD_RELEASED,
        .ppsd = NULL,
        //.est = { 0 },
        // TODO default arguments
        .p_pps = { .path = PPS_PATH,
                   .capture_assert = PPS_ASSERT,
                   .hw_offset_ns = PPS_HW_OFF_ns },
        .p_stat = { .long_s = 96, .shrt_s = 16 },
        .p_off = { .min_ns = -500*1000*1000,
                   .max_ns = +500*1000*1000,
                   .corr_max_s = 8 },
        .p_drift = { .max_ppb = 1000*1000,
                     .min_ppb = 10 },
        //.stats_options = PPS_STATS_PRINT_ABS_TREF
    };
    return &_FSM_;
}

struct param_pps_t * ppsd_param_pps(void) {
    return &ppsd_fsm()->p_pps;
}

struct param_stat_t * ppsd_param_stat(void) {
    return &ppsd_fsm()->p_stat;
}

struct param_offset_t * ppsd_param_offset(void) {
    return &ppsd_fsm()->p_off;
}

struct param_drift_t * ppsd_param_drift(void) {
    return &ppsd_fsm()->p_drift;
}

// Each state is 3 functions:
//    - enter the state
//    - run the state, ppsd_update is mandatory
//    - quit the state
struct state_fun {
    enum ppsd_state_t (*enter) (struct ppsd_fsm_t * fsm);
    int               (*run)   (struct ppsd_fsm_t * fsm);
    enum ppsd_state_t (*quit)  (struct ppsd_fsm_t * fsm);
};

static int _state_run (struct ppsd_fsm_t * fsm,
                       long double predict_ns,
                       long double dist2predict_max_ns) {
    int ok = ppsd_update(fsm->ppsd, predict_ns, dist2predict_max_ns);
    if (ok > 0) {
        pps_stats_flast(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    } else if (ok == 0) {
        // outlier
        fcmt(ppsdout, "%ld, %+9lld\n",
             ppsd_timeref(fsm->ppsd)->tv_sec,
             ppsd_offset_ns(fsm->ppsd));
    }
    return ok;
}

static enum ppsd_state_t _state_quit (struct ppsd_fsm_t * fsm) {
    return fsm->state;
}

// INITIALIZED state ///////////////////////////////////////////////////////////
static enum ppsd_state_t _init_enter (struct ppsd_fsm_t * fsm) {
    ass(fsm->state == PPSD_RELEASED);
    fsm->ppsd = ppsd_open(fsm->p_pps.path,
                          fsm->p_pps.capture_assert,
                          fsm->p_pps.hw_offset_ns);
    if (fsm->ppsd == NULL) {
        return fsm->state; // PPSD_RELEASED;
    }
    unsigned int n_pps = fsm->p_stat.shrt_s > fsm->p_stat.long_s
                       ? fsm->p_stat.shrt_s : fsm->p_stat.long_s;
    ppsd_stats_init(fsm->ppsd, n_pps);
    if (ppsd_stats(fsm->ppsd) == NULL) {
        ppsd_close(fsm->ppsd);
        fsm->ppsd = NULL;
        return fsm->state; // PPSD_RELEASED;
    }
    if (adjtimex_snapshot(&fsm->tx)) {
        // What TODO ???
    }
    fsm->state = PPSD_INITIALIZED;
    return fsm->state; // PPSD_INITIALIZED;
}

static int _init_run (struct ppsd_fsm_t * fsm) {
    return _state_run(fsm, 0.0L, 0.0L);
}

static struct state_fun _init_fun = {
    .enter = _init_enter,
    .run   = _init_run,
    .quit  = _state_quit,
};

// RELEASED state //////////////////////////////////////////////////////////////
static enum ppsd_state_t _release_enter (struct ppsd_fsm_t * fsm) {
    ass(fsm->state != PPSD_RELEASED);
    long adj_ppb = adjtimex_get_freq();
    slogout("%s CLK freq %+ldppb\n", SLOG_CMT_STR, adj_ppb);
    ppsd_stats_release(fsm->ppsd);
    ppsd_close(fsm->ppsd);
    fsm->ppsd = NULL;
    fsm->state = PPSD_RELEASED;
    return fsm->state; // PPSD_RELEASED;
}

static struct state_fun _release_fun = {
    .enter = _release_enter,
    .run   = NULL,
    .quit  = _state_quit,
};

static void ppsd_fsm_reset (struct ppsd_fsm_t * fsm) {
    ppsd_stats_reset(fsm->ppsd);
    fsm->est.drift_ppb = 0.0L;
    fsm->est.offset_ns = 0.0L;
    fsm->est.stddev_ns = 0.0L;
}

// LONG STAT state /////////////////////////////////////////////////////////////
static enum ppsd_state_t _long_stat_enter (struct ppsd_fsm_t * fsm) {
    if (false) {
        ppsd_fsm_reset(fsm);
    }
    fsm->state_s = 0u;
    fsm->stats_options = PPS_STATS_PRINT_ABS_TREF
                            | PPS_STATS_PRINT_MEAN
                            | PPS_STATS_PRINT_STDDEV
                            | PPS_STATS_PRINT_DRIFT
                            | PPS_STATS_PRINT;
    pps_stats_header(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    fsm->state = PPSD_LONG_STAT;
    return fsm->state;
}

static int _long_stat_run (struct ppsd_fsm_t * fsm) {
    // TODO optional filtering ?
    int ret = _state_run(fsm, 0.0L, 0.0L);
    if (ret <= 0) {
        return ret;
    }
    fsm->state_s ++;
    if (fsm->state_s < fsm->p_stat.long_s) {
        return 0;
    }
    return 1;
}

static void ppsd_estimate(struct ppsd_fsm_t * fsm, unsigned int win) {
    estimate_set(&fsm->est, fsm->ppsd, win);
}

static enum ppsd_state_t _long_stat_quit (struct ppsd_fsm_t * fsm) {
    pps_stats_header2(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    slogout("%s", SLOG_CMT_STR);
    pps_stats_fprint(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    ppsd_estimate(fsm, 0u);
    return fsm->state;
}

static struct state_fun _long_stat_fun = {
    .enter = _long_stat_enter,
    .run   = _long_stat_run,
    .quit  = _long_stat_quit,
};

// SHRT STAT ///////////////////////////////////////////////////////////////////
static enum ppsd_state_t _shrt_stat_enter (struct ppsd_fsm_t * fsm) {
    fsm->stats_options = 0u; // print sorted when quit
    ppsd_estimate(fsm, fsm->p_stat.shrt_s);
    if (false) {
        ppsd_fsm_reset(fsm);
    }
    fsm->state_s = 0u;
    fsm->state = PPSD_SHRT_STAT;
    return fsm->state;
}

static int _shrt_stat_run (struct ppsd_fsm_t * fsm) {
    // TODO 3.0 x stddev ?
    int ret = _state_run(fsm,
                         estimate_next(&fsm->est),
                         0.0l*fsm->est.stddev_ns);
    // FIXME if too many outliers increase the max distance allowed
    if (ret <= 0) {
        // TODO really ?
        if (ret == 0) {
            //fsm->est.offset_ns = estimate_next(&fsm->est);
            //fsm->est.offset_ns = ppsd_offset_ns(&fsm->ppsd);
        }
        return ret;
    }
    //fsm->est.offset_ns = ppsd_offset_ns(&fsm->ppsd);
    fsm->state_s ++;
    if (fsm->state_s < fsm->p_stat.shrt_s) {
        return 0;
    }
    return 1;
}

static enum ppsd_state_t _shrt_stat_quit (struct ppsd_fsm_t * fsm) {
    fsm->stats_options = PPS_STATS_PRINT_ABS_TREF
                            | PPS_STATS_PRINT_SORTED
                            | PPS_STATS_PRINT_INFO
                            | PPS_STATS_PRINT;
    pps_stats_flog(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    fsm->stats_options |= PPS_STATS_PRINT_MEAN
                            | PPS_STATS_PRINT_STDDEV
                            | PPS_STATS_PRINT_MEDIAN
                            | PPS_STATS_PRINT_DRIFT
                            | PPS_STATS_PRINT;
    pps_stats_header2(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    pps_stats_fprint(ppsdout, ppsd_stats(fsm->ppsd), fsm->stats_options);
    ppsd_estimate(fsm, fsm->p_stat.shrt_s);
    return fsm->state;
}

static struct state_fun _shrt_stat_fun = {
    .enter = _shrt_stat_enter,
    .run   = _shrt_stat_run,
    .quit  = _shrt_stat_quit,
};

#define POS(x) ( ((x) < 0.0L) ? (-(x)) : (+(x)) )

// DRIFT_CORR state ////////////////////////////////////////////////////////////
static enum ppsd_state_t _drift_corr_enter (struct ppsd_fsm_t * fsm) {
    // TODO if (fsm->state != PPSD_DRIFT_EVAL) ?
    // TODO snapshot adjtimex
    if (POS(fsm->est.drift_ppb) > fsm->p_drift.max_ppb) {
        slogerr("#|%+Lf| > %Lfppb, do nothing\n",
                fsm->est.drift_ppb,
                fsm->p_drift.max_ppb);
    } else if (POS(fsm->est.drift_ppb) < fsm->p_drift.min_ppb) {
        slogerr("#|%+Lf| < %Lfppb, do nothing\n",
                fsm->est.drift_ppb,
                fsm->p_drift.min_ppb);
    } else {
        // TODO error ?
        long current_ppb = adjtimex_get_freq();
        long ppb = roundl(fsm->est.drift_ppb);
        int ret = adjtimex_adj_freq(-ppb);
        slogout("%s CLK freq %+ldppb adj by %+.0Lfppb return %+d (%+ldppb)\n",
                SLOG_CMT_STR, current_ppb, -fsm->est.drift_ppb, ret, 
                adjtimex_get_freq());
        ppsd_fsm_reset(fsm);
    }
    //fsm->state = PPSD_DRIFT_CORR;
    return fsm->state;
}

static int _drift_corr_run (struct ppsd_fsm_t * fsm) {
    return _state_run(fsm, 0.0L, 0.0L);
}

static enum ppsd_state_t _drift_corr_quit (struct ppsd_fsm_t * fsm) {
    slogout("%s CLK freq %+ldppb\n", SLOG_CMT_STR, adjtimex_get_freq());
    return fsm->state;
}

static struct state_fun _drift_corr_fun = {
    .enter = _drift_corr_enter,
    .run   = _drift_corr_run,
    .quit  = _drift_corr_quit,
};

// OFFSET_CORR /////////////////////////////////////////////////////////////////
static enum ppsd_state_t _offset_corr_enter (struct ppsd_fsm_t * fsm) {
    // TODO if fsm->state != PPSD_OFFSET_EVAL
    //fsm->state = PPSD_OFFSET_CORR;
    /*
    if (fsm->p_off.corr_max_s > 0) {
        if ( (fsm->est.offset_ns < fsm->p_off.min_ns)
                || (fsm->est.offset_ns > fsm->p_off.max_ns) ) {
            int ret = adjtimex_set_offset(-roundl(fsm->est.offset_ns));
            fcmt(ppsdout,
                 "offset %+.0Lfns, brutal offset correction (%+d).\n",
                 fsm->est.offset_ns, ret);
            fsm->p_off.corr_s = 0;
            ppsd_fsm_reset(fsm);
        } else {
            // keep track of the value used to set the clk back
            long double clk_ppb = roundl(fsm->est.offset_ns);
            int ret = adjtimex_adj_freq(+clk_ppb);
            fcmt(ppsdout,
                 "adjust clk by %+.0Lfppb during %us (%+d).\n",
                 fsm->est.offset_ns, fsm->p_off.corr_s, ret);
            fsm->p_off.corr_s = 1;
            ppsd_fsm_reset(fsm);
            fsm->est.drift_ppb = clk_ppb;
        }
    }
    */
    return fsm->state;
}

static int _offset_corr_run (struct ppsd_fsm_t * fsm) {
    int ret = _state_run(fsm, 0.0L, 0.0L);
    if (ret <= 0) {
        return ret;
    }
    if (fsm->p_off.corr_s == 0) {
        return 1;
    } else {
        fsm->p_off.corr_s --;
    }
    if (fsm->p_off.corr_s > 0) {
        return 0;
    }
    return 1; // TODO
}

static enum ppsd_state_t _offset_corr_quit (struct ppsd_fsm_t * fsm) {
    /*
    if ( (fsm->est.drift_ppb > +0.5l) || (fsm->est.drift_ppb < -0.5l) ) {
        int ret = adjtimex_adj_tick_freq(0, -fsm->est.drift_ppb);
        fcmt(ppsdout,
             "adjust clk back by %+.0Lfppb (%+d).\n",
             -fsm->est.drift_ppb, ret);
        ppsd_fsm_reset(fsm);
    }
    */
    return fsm->state;
}

static struct state_fun _offset_corr_fun = {
    .enter = _offset_corr_enter,
    .run   = _offset_corr_run,
    .quit  = _offset_corr_quit,
};

////////////////////////////////////////////////////////////////////////////////
enum ppsd_state_t ppsd_get_state(struct ppsd_fsm_t const * fsm) {
    return fsm->state;
}

enum ppsd_state_t ppsd_run(struct ppsd_fsm_t * fsm, enum ppsd_state_t state) {
    
    // STATICS ///////////////////////////////////////
    static char const * STATES_LIT[PPSD_STATES_NB] = {
        "RELEASED   ",
        "INITIALIZED",
        "LONG_STAT",
        "SHRT_STAT",
    };
    static struct state_fun ppsd_fun[PPSD_STATES_NB];
    static bool _NOT_READY = true;
    if (_NOT_READY) {
        slogdbg("%s\n", "### INITIALIZING PPSD FSM ###");
        // INIT STATE FUNCTIONS
        ppsd_fun[PPSD_RELEASED] = _release_fun;
        ppsd_fun[PPSD_INITIALIZED] = _init_fun;
        ppsd_fun[PPSD_LONG_STAT] = _long_stat_fun;
        ppsd_fun[PPSD_SHRT_STAT] = _shrt_stat_fun;
        //ppsd_fun[PPSD_OFFSET_CORR] = _offset_corr_fun;
        //ppsd_fun[PPSD_DRIFT_CORR] = _drift_corr_fun;
        _NOT_READY = false;
    }
    //////////////////////////////////////////////////

    ass(state >= PPSD_RELEASED);
    ass(state < PPSD_STATES_NB);
    ass(fsm->state >= PPSD_RELEASED);
    ass(fsm->state < PPSD_STATES_NB);

    // FIXME FIXME FIXME...
    // This is a mess,
    //    - how to quit / enter the same state ?
    //    - do not reset stats (windowed), but another count for stateduration ?
    if (state != fsm->state) {
        // quit fsm->state
        slogerr("### Quit  %s ###\n", STATES_LIT[fsm->state]);
        ppsd_fun[fsm->state].quit(fsm);
        // enter state TODO TODO TODO
        if ((state == PPSD_LONG_STAT) && (fsm->p_stat.long_s == 0)) {
            state = PPSD_SHRT_STAT;
        }
        if ((state == PPSD_SHRT_STAT) && (fsm->p_stat.shrt_s == 0)) {
            state = PPSD_LONG_STAT;
        }
        //if ((state == PPSD_OFFSET_CORR) || (state == PPSD_DRIFT_CORR)) {
        //    state = PPSD_SHRT_STAT;
        //}
        slogerr("### Enter %s ###\n", STATES_LIT[state]);
        ppsd_fun[state].enter(fsm);
    }
    if (PPSD_RELEASED == fsm->state) {
        slogerr("!!! %s !!!\n", STATES_LIT[fsm->state]);
        return PPSD_RELEASED;
    }

    ass(fsm->state > PPSD_RELEASED);
    ass(fsm->state < PPSD_STATES_NB);
    
    int transition = ppsd_fun[fsm->state].run(fsm);
    if (transition > 0) {
        return fsm->state + 1;
    }
    return transition == 0 ? fsm->state : PPSD_RELEASED;
}

///////////////////////////////////////////////////////////////////////////////
#ifdef PPSD_FSM_MAIN
#include "version.h"

int main (int argc, char ** argv) {
    
    slogcmt("%s %s %s\n\n", argv[0], VERSION, COMMIT_DATE);

    struct ppsd_fsm_t* fsm = ppsd_fsm();
    enum ppsd_state_t state = ppsd_run(fsm, fsm->state + 1);
    while (state != PPSD_RELEASED) {
        state = ppsd_run(fsm, state);
    }
    exit ((state != PPSD_RELEASED) ? EXIT_SUCCESS : EXIT_FAILURE);
}

#endif

