#include "pps_helper.h"
#include "timespec_helper.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/timepps.h>
#include <time.h>

FILE* pps_stdout = NULL;
FILE* pps_stderr = NULL;
FILE* pps_stddbg = NULL;
#define STDOUT pps_stdout
#define STDERR pps_stderr
#define STDDBG pps_stddbg
#include "slog.h"

/*****************************************************************************
 *
 *****************************************************************************/
struct pps_t {
    char const * path;
    int dev;
    pps_handle_t handle;
    bool capture_assert;
};

/*****************************************************************************
 *
 *****************************************************************************/
void pps_close(struct pps_t * pps) {
    // TODO assume time_pps_destroy make a close on the device passed
    if (pps->handle > 0) {
        time_pps_destroy(pps->handle);
        pps->handle = 0;
        pps->dev = 0;
    } else if (pps->dev > 0) {
        close(pps->dev);
        pps->dev = 0;
    } else {}
}

/*****************************************************************************
 *
 *****************************************************************************/
static int _pps_open(struct pps_t * pps,
                     char const * path,
                     bool capture_assert) {
    pps->dev = open(path, O_RDWR);
    if (pps->dev < 0) {
        slogout("\"%s\" open error %s!\n", path, strerror(errno));
        return -1;
    }
    pps->path = path;
    if (time_pps_create(pps->dev, &pps->handle) < 0) {
        slogout("%s\" is not a PPS, %s\n", pps->path, strerror(errno));
        close (pps->dev);
        return -1;
    }

    int pps_mode = 0;
    if (time_pps_getcap(pps->handle, &pps_mode) < 0) {
        slogout("Can not get \"%s\" capabilies, %s\n",
                pps->path,
                strerror(errno));
        pps_close(pps);
        return -1;
    }
    if (!(pps_mode & PPS_CANWAIT)) {
        slogout("\"%s\": can not wait\n", pps->path);
        pps_close(pps);
        return -1;
    }

    pps->capture_assert = capture_assert;
    if (pps->capture_assert && !(pps_mode & PPS_CAPTUREASSERT)) {
        slogout("\"%s\": assert not supported\n", pps->path);
        pps->capture_assert = false;
    } else if (!pps->capture_assert && !(pps_mode & PPS_CAPTURECLEAR)) {
        slogout("\"%s\": clear not supported\n", pps->path);
        pps->capture_assert = true;
    } else {}

    pps_params_t params;
    time_pps_getparams(pps->handle, &params);
    // TODO zeroing PPS offset
    if (pps->capture_assert) {
        params.mode |= PPS_CAPTUREASSERT;
        params.mode &= ~PPS_CAPTURECLEAR;
    } else {
        params.mode |= PPS_CAPTURECLEAR;
        params.mode &= ~PPS_CAPTUREASSERT;
    }
    if (time_pps_setparams(pps->handle, &params)) {
        slogout("\"%s\": can not set params %s\n",
                pps->path,
                strerror(errno));
        pps_close(pps);
        return -1;
    }
    return pps->handle;
}

static struct pps_t _PPS_ = {
    .path = "",
    .dev = -1,
};

struct pps_t * pps_open(char const * path, bool capture_assert) {
    if (_pps_open(&_PPS_, path, capture_assert) < 0) {
        return NULL;
    }
    return &_PPS_;
}

/*****************************************************************************
 *
 *****************************************************************************/
#ifndef PPS_TIMEOUT_s
    #define PPS_TIMEOUT_s 1
#endif
#ifndef PPS_TIMEOUT_ns
    #define PPS_TIMEOUT_ns 500000000
#endif

#define PPS_SPURIOUS_CHECK_ns 0
#ifndef PPS_SPURIOUS_CHECK_ns
    #define PPS_SPURIOUS_CHECK_ns 750000000
#endif

int pps_get_timestamp(struct pps_t const * pps,
                      struct timespec * pps_timestamp) {

    static struct timespec prev_pps_ts = { 0 };

    struct timespec PPS_TO = {
        .tv_sec = PPS_TIMEOUT_s,
        .tv_nsec = PPS_TIMEOUT_ns,
    };
    long long PPS_TO_ns = timespec2ns(&PPS_TO);
    
    pps_info_t pps_info;
    struct timespec t0 = { 0 };
    struct timespec t1 = { 0 };
    struct timespec dt = { 0 };
    clock_gettime(CLOCK_REALTIME, &t0);
    int ret = time_pps_fetch (pps->handle,
                              PPS_TSFMT_TSPEC,
                              &pps_info,
                              &PPS_TO);
    clock_gettime(CLOCK_REALTIME, &t1);
    timespec_diff(&t1, &t0, &dt);
    long long dt_ns = timespec2ns(&dt);
    //slogcmt(">>> %ld.%ld -> %ld.%ld = %lldns <<<\n",
    //		    t0.tv_sec, t0.tv_nsec,
    //		    t1.tv_sec, t1.tv_nsec, dt_ns);
    if (ret < 0) {
        if (PPS_TO_ns > dt_ns) {
            slogdbg("PPS spurious timeout (%lldns < %lldns): %s.\n",
                    dt_ns,
                    PPS_TO_ns,
                    strerror(errno));
            errno = 0;
            return pps_get_timestamp(pps, pps_timestamp);
        }
        slogcmt("PPS timeout (%lldns > %lldns): %s.\n",
                dt_ns,
                PPS_TO_ns,
                strerror(errno));
    } else {
        *pps_timestamp = pps->capture_assert
                            ? pps_info.assert_timestamp
                            : pps_info.clear_timestamp; 
	if (PPS_SPURIOUS_CHECK_ns > 0) {
            timespec_diff(pps_timestamp, &prev_pps_ts, &dt);
            dt_ns = timespec2ns(&dt);
	    if ((dt_ns > 0) && (dt_ns < PPS_SPURIOUS_CHECK_ns)) {
                slogdbg("previous PPS is %lldns old, spurious ?\n", dt_ns);
                // FIXME !!!
                return pps_get_timestamp(pps, pps_timestamp);
	    }
	}
	prev_pps_ts = *pps_timestamp;
        if (0) {
        timespec_diff(&t1, pps_timestamp, &dt);
        slogdbg("PPS fetch delay %lldns, duration %lldns.\n",
                timespec2ns(&dt), dt_ns);
        }
    }

    return ret;
}

/*****************************************************************************
 *
 *****************************************************************************/
int pps_set_clock(struct pps_t const * pps,
                  struct timespec * timestamp,
                  struct timespec const * timeref) {
    // Rise the priority at its max to reduce delay between time_pps_fetch
    // and clock_settime
    int prio = getpriority(PRIO_PROCESS, 0);
    int ok = 0;
    if (setpriority(PRIO_PROCESS, 0, -20) < 0) {
        slogcmt("Can not increase prority: %s\n", strerror(errno));
    }
    if (pps_get_timestamp (pps, timestamp) < 0) {
        slogout("%s\n", "PPS error, can not set the clock.");
        ok = -1;
    } else {
        // TODO if time diff is less than 100ms or so, use adjtimex !
        struct timespec timeset = (timeref == NULL) ? *timestamp : *timeref;
        if (timeref != NULL) {
            timeset = *timeref;
        } else {
            timeset = *timestamp;
            timeset.tv_nsec  = 0;
            if (timestamp->tv_nsec >= ns_per_s / 2) {
                timeset.tv_sec ++;
            }
        }
        ok = clock_settime(CLOCK_REALTIME, &timeset);
        if (ok < 0) {
            slogout("Can not set the clock: %s\n", strerror(errno));
        } else {
            slogcmt("Clock updated from %ld.%09ld to %ld.%09ld\n",
                    timestamp->tv_sec,
                    timestamp->tv_nsec,
                    timeset.tv_sec,
                    timeset.tv_nsec);
        }
    }
    if (setpriority(PRIO_PROCESS, 0, prio) < 0) {
        slogcmt("Can not go back to prority +%d: %s\n", prio, strerror(errno));
    }
    return ok;
}

/*****************************************************************************
 * TODO TODO TODO
 * If I understood correctly, call this, then STA_PPSFREQ and STA_PPSTIME are
 * set by the kernel.
 * TODO PPS_KC_REMOVE ???
 *****************************************************************************/
int pps_hardpps(struct pps_t const * pps) {
    struct pps_bind_args args;
    args.tsformat = PPS_TSFMT_TSPEC;
    args.consumer = PPS_KC_HARDPPS;
    args.edge = pps->capture_assert ? PPS_CAPTUREASSERT : PPS_CAPTURECLEAR;
    int ret = ioctl(pps->dev, PPS_KC_BIND, &args);
    if (ret < 0) {
        slogout("\"%s\": can not kc bind %s (%d, %d).\n",
                pps->path,
                strerror(errno),
                ret,
                errno);
    }
    return ret;
}

