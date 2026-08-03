#include "ppsd_fsm.h"
#include "drift_file.h"
#include "ppsd.h"
#include "timeref.h"

struct ppsd_line_t {
    struct timespec timeref;
    struct timespec timestamp;
    long long drift_adj_ppb;
    long long drift_tot_ppb;
};

struct ppsd_fsm_t {
    struct timespec timeref;
    struct ppsd_t * ppsd;
    struct ppsd_params_t params;
    struct ppsd_line_t last_line;
};

static struct ppsd_fsm_t _FSM_ = {
    .timeref = { 0 },
    .ppsd = NULL,
    .params = { 0 },
    .last_line = { 0 },
};

struct ppsd_fsm_t * ppsd_fsm_init (struct ppsd_params_t * const params) {
    _FSM_.ppsd = ppsd_open(params->pps_path,
                           params->pps_capture_assert,
                           params->pps_hw_offset_ns);
    if (NULL == _FSM_.ppsd) {
        return NULL;
    }
    _FSM_.params = *params;
    return &_FSM_;
}

struct timespec const * ppsd_fsm_timeref(struct ppsd_fsm_t * fsm) {
    struct timespec timeref = {0};
    struct timespec timenow = {0};
    if (gpsd_timeref(fsm->params.gpsd_addr,
                     fsm->params.gpsd_port,
                     &timeref, &timenow) <= 0) {
        return NULL;
    }
    ppsd_set_timeref(fsm->ppsd, &timeref);
    return ppsd_timeref(fsm->ppsd);
}

int ppsd_fsm_read_file(struct ppsd_fsm_t * fsm) {
    return drift_fread (fsm->params.file_path,
                        &fsm->last_line.timeref,
                        &fsm->last_line.timestamp,
                        &fsm->last_line.drift_adj_ppb,
                        &fsm->last_line.drift_tot_ppb);
}

void ppsd_fsm_release (struct ppsd_fsm_t * fsm) {
    ppsd_close(fsm->ppsd);
    fsm->ppsd = NULL;
}

