#ifndef PPSD_FSM_H
#define PPSD_FSM_H

#include <stdbool.h>

struct ppsd_params_t {
    // PPS
    char const * pps_path;
    bool pps_capture_assert;
    long pps_hw_offset_ns;
    // GPSD
    char const * gpsd_addr;
    char const * gpsd_port;
    // Persistent file
    char const * file_path;
};

struct ppsd_fsm_t;

struct ppsd_fsm_t * ppsd_fsm_init (struct ppsd_params_t * const params);

void ppsd_fsm_release (struct ppsd_fsm_t * fsm);

#endif // PPSD_FSM_H

