/*****************************************************************************
 *
 *****************************************************************************/
#include "adjtimex_helper.h"
#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/timex.h>

// man 2 adjtimex for the syscall
// man 8 adjtimex for the application

FILE* adjtimex_stdout = NULL;
#define STDOUT adjtimex_stdout
FILE* adjtimex_stderr = NULL;
#define STDERR adjtimex_stderr
FILE* adjtimex_stddbg = NULL;
#define STDDBG adjtimex_stddbg
#include "slog.h"

///////////////////////////////////////////////////////////////////////////////
static char const * adjtimex_return_str(int ret) {
    //static char ret_str[256] = {'\0'};
    char const * ret_str = NULL;
    switch (ret) {
        case TIME_OK:
        ret_str = "CLK SYNC";
        break;
        case TIME_INS:
        ret_str = "LEAP INS PENDING";
        break;
        case TIME_DEL:
        ret_str = "LEAP DEL PENDING";
        break;
        case TIME_OOP:
        ret_str = "LEAP IN PROGRESS";
        break;
        case TIME_WAIT:
        ret_str = "LEAP COMPLETE";
        break;
        case TIME_ERROR:
        ret_str = "TIME ERROR";
        break;
        case -1:
        ret_str = "ERROR !!!";
        break;
        default:
        ret_str = "??? UNKNOWN ???";
        break;
    }
    return ret_str;
}

static char const * adjtimex_modes_str(int modes) {
    static char modes_str[256] = { '\0' };
    modes_str[0] = '\0';
    // ADJ_OFFSET supplied value is clamped to the range -/+0.5s
    if (modes & ADJ_OFFSET) { strcat(modes_str, " OFFSET"); }
    // ADJ_FREQUENCY supplied value is clamped to the range -/+32768000
    if (modes & ADJ_FREQUENCY) { strcat(modes_str, " FREQUENCY"); }
    // ADJ_MAXERROR Set maximum time error from buf.maxerror.
    if (modes & ADJ_MAXERROR) { strcat(modes_str, " MAXERROR"); }
    // ADJ_ESTERROR Set estimated time error from buf.esterror.
    if (modes & ADJ_ESTERROR) { strcat(modes_str, " ESTERROR"); }
    // ADJ_STATUS Set clock status bits from buf.status.
    if (modes & ADJ_STATUS) { strcat(modes_str, " STATUS"); }
    // ADJ_TIMECONST Set PLL time constant from buf.constant. If the STA_NANO
    // status flag is clear, the kernel adds 4 to this value.
    if (modes & ADJ_TIMECONST) { strcat(modes_str, " TIMECONST"); }
    if (modes & ADJ_SETOFFSET) { strcat(modes_str, " SETOFFSET"); }
    if (modes & ADJ_MICRO) { strcat(modes_str, " MICRO"); }
    if (modes & ADJ_NANO) { strcat(modes_str, " NANO"); }
    // ADJ_TAI Set TAI offset from buf.constant.
    if (modes & ADJ_TAI) { strcat(modes_str, " TAI"); }
    // ADJ_TICK Set tick value from buf.tick.
    if (modes & ADJ_TICK) { strcat(modes_str, " TICK"); }
    // ADJ_OFFSET_SINGLESHOT Old-fashioned adjtime(3).
    if (modes & ADJ_OFFSET_SINGLESHOT) { 
        strcat(modes_str, " OFFSET_SINGLESHOT");
    }
    // ADJ_OFFSET_SS_READ Return (in buf.offset) the remaining amount of time
    // to be adjusted after an earlier ADJ_OFFSET_SINGLESHOT operation.
    if ((modes & ADJ_OFFSET_SS_READ) == ADJ_OFFSET_SS_READ) {
        strcat(modes_str, " OFFSET_SS_READ");
    }
    return modes_str;
}

static char const * adjtimex_status_str(int status) {
    static char status_str[256] = { '\0' };
    status_str[0] = '\0';
    // STA_PLL (rw) Enable phase-locked loop (PLL) updates via ADJ_OFFSET.
    if (status & STA_PLL) { strcat(status_str, " PLL"); }
    // STA_PPSFREQ (rw) Enable PPS (pulse-per-second) frequency discipline.
    if (status & STA_PPSFREQ) { strcat(status_str, " PPSFREQ"); }
    // STA_PPSTIME (rw) Enable PPS time discipline.
    if (status & STA_PPSTIME) { strcat(status_str, " PPSTIME"); }
    // STA_FLL (rw) Select frequency-locked loop (FLL) mode.
    if (status & STA_FLL) { strcat(status_str, " FLL"); }
    if (status & STA_INS) { strcat(status_str, " INS"); }
    if (status & STA_DEL) { strcat(status_str, " DEL"); }
    // STA_UNSYNC (rw) Clock unsynchronized.
    if (status & STA_UNSYNC) { strcat(status_str, " UNSYNC"); }
    /* STA_FREQHOLD (rw)
              Hold frequency.  Normally adjustments made via ADJ_OFFSET result
              in dampened frequency adjustments also being made.  So a single  
              call  corrects  the current offset, but as offsets in the same 
              direction are made repeatedly, the small frequency adjustments 
              will accumulate to fix the long-term skew.
              This flag prevents the small frequency adjustment from being made 
              when correcting for an ADJ_OFFSET value.
    */
    if (status & STA_FREQHOLD) { strcat(status_str, " FREQHOLD"); }
    // STA_PPSSIGNAL (ro) A valid PPS signal is present.
    if (status & STA_PPSSIGNAL) { strcat(status_str, " PPSSIGNAL"); }
    // STA_PPSJITTER (ro) PPS signal jitter exceeded.
    if (status & STA_PPSJITTER) { strcat(status_str, " PPSJITTER"); }
    // STA_PPSWANDER (ro) PPS signal wander exceeded.
    if (status & STA_PPSWANDER) { strcat(status_str, " PPSWANDER"); }
    // STA_PPSERROR (ro) PPS signal calibration error.
    if (status & STA_PPSERROR) { strcat(status_str, " PPSERROR"); }
    // STA_CLOCKERR (ro) Clock hardware fault.
    if (status & STA_CLOCKERR) { strcat(status_str, " CLOCKERR"); }
    // STA_NANO (ro) 0 = us, 1 = ns. Set ADJ_NANO, cleared ADJ_MICRO.
    if (status & STA_NANO) { strcat(status_str, " NANO"); }
    // STA_MODE 0 = PLL, 1 = FLL
    if (status & STA_MODE) {
        strcat(status_str, " MODE_FLL");
    } else {
        strcat(status_str, " MODE_PLL");
    }
    // STA_CLK (ro) Clock source (0 = A, 1 = B); currently unused.
    if (status & STA_CLK) { strcat(status_str, " CLK"); }
    return status_str;
}

static bool _timex_ns(struct timex const * tx) {
    return (tx->status & STA_NANO) == STA_NANO;
}

static char const * _timex_ns_us_str(struct timex const * tx) {
    return _timex_ns(tx) ? "ns" : "us";
}

static double _q162ppm (long q16) {
    return ((double)q16) / 65536.0f;
}

void timex_flog(FILE* file, struct timex const * tx) {
    if (_timex_ns(tx)) {
        fcmt(file, "time   %ld.%09ldns\n", tx->time.tv_sec, tx->time.tv_usec);
    } else {
        fcmt(file, "time   %ld.%06ldus\n", tx->time.tv_sec, tx->time.tv_usec);
    }
    fcmt(file, "mode   0x%08X %s\n",
         tx->modes, adjtimex_modes_str(tx->modes));
    fcmt(file, "status 0x%08X %s\n",
         tx->status, adjtimex_status_str(tx->status));
    fcmt(file, "freq   %+lfppm\n", _q162ppm(tx->freq));
    fcmt(file, "offset %+ld%s\n", tx->offset, _timex_ns_us_str(tx));
    fcmt(file, "max error %ldus\n", tx->maxerror);
    fcmt(file, "est error %ldus\n", tx->esterror);
    fcmt(file, "PLL constant %ld\n", tx->constant);
    fcmt(file, "CLK precision %ldus\n", tx->precision);
    fcmt(file, "CLK tolerance %lfppm\n", _q162ppm(tx->tolerance));
    fcmt(file, "CLK tick %ldus\n", tx->tick);
    fcmt(file, "PPS frequency %+lfppm\n", _q162ppm(tx->ppsfreq));
    fcmt(file, "PPS jitter %ld%s\n", tx->jitter, _timex_ns_us_str(tx));
    fcmt(file, "PPS jitter exceed %ld\n", tx->jitcnt);
    fcmt(file, "PPS shift %ds\n", tx->shift);
    fcmt(file, "PPS stability %+lfppm\n", _q162ppm(tx->stabil));
    fcmt(file, "PPS stability exceed %ld\n", tx->stbcnt);
    fcmt(file, "PPS calibration %ld\n", tx->calcnt);
    fcmt(file, "PPS calibration error %ld\n", tx->errcnt);
    fcmt(file, "TAI offset %+d\n", tx->tai);
}

void adjtimex_log(FILE* file) {
    struct timex tx = { 0 };
    //tx.status |= ADJ_NANO; TODO ???
    int ret = adjtimex(&tx);
    if (ret < 0) {
        slogout("adjtimex returns %s (%+d)\n", adjtimex_return_str(ret), ret);
    } else {
        slogdbg("adjtimex returns %s (%+d)\n", adjtimex_return_str(ret), ret);
    }
    timex_flog(file, &tx);
}

///////////////////////////////////////////////////////////////////////////////
int timex_cmp(struct timex const * old_tx, struct timex const * new_tx) {
    int cmp = 0;
    if (old_tx->modes != new_tx->modes) { 
        slogdbg("modes 0x%08X (%s) -> 0x%08X (%s)\n",
                old_tx->modes, adjtimex_modes_str(old_tx->modes),
                new_tx->modes, adjtimex_modes_str(new_tx->modes));
        cmp |= 0x01;
    }
    if (old_tx->status != new_tx->status) { 
        slogdbg("status 0x%08X (%s) -> 0x%08X (%s)\n",
                (unsigned) old_tx->status, adjtimex_status_str(old_tx->status),
                (unsigned) new_tx->status, adjtimex_status_str(new_tx->status));
        cmp |= 0x02;
    }
    if (old_tx->freq != new_tx->freq) { 
        slogdbg("freq %+lfppm (%+ld) -> freq %+lfppm (%+ld) \n",
                _q162ppm(old_tx->freq), old_tx->freq,
                _q162ppm(new_tx->freq), new_tx->freq);
        cmp |= 0x04;
    }
    if (old_tx->offset != new_tx->offset) { 
        slogdbg("offset %ld%s -> %ld%s\n",
                old_tx->offset, _timex_ns_us_str(old_tx),
                new_tx->offset, _timex_ns_us_str(new_tx));
        cmp |= 0x08;
    }
    if (old_tx->tick != new_tx->tick) { 
        slogdbg("tick %ldus -> %ldus\n", old_tx->tick, new_tx->tick);
        cmp |= 0x10;
    }
    /* TODO
    tx->maxerror
    tx->esterror
    tx->constant
    tx->precision
    tx->tolerance
    tx->ppsfreq
    tx->jitter
    tx->jitcnt
    tx->shift
    tx->stabil
    tx->stbcnt
    tx->calcnt
    tx->errcnt
    tx->tai
    TODO */
    return cmp;
}

///////////////////////////////////////////////////////////////////////////////
// Thread safe no more
//static struct timex _tx = { 0 };

static int _adjtimex_snapshot(struct timex * tx, char const * str) {
/*    if (NULL == tx) {
        if ((_tx.time.tv_sec == 0) && (_tx.time.tv_usec == 0)) {
            slogcmt("%s\n", "adjtimex timex init");
        }
        tx = &_tx;
    }*/
    //int ret = clock_adjtime(CLOCK_REALTIME, tx);
    int ret = adjtimex(tx);
    if (ret < 0) {
        slogout("adjtimex(%s) return \"%s\" (%d)\n",
                (str == NULL) ? "" : str, strerror(errno), errno);
    }
    if (str != NULL) {
        slogdbg("%c", '\n');
        slogdbg("adjtimex(%s) returns %s (%d).\n",
                str, adjtimex_return_str(ret), ret);
        timex_flog(STDDBG, tx);
        slogdbg("%c", '\n');
    }
    return ret;
}

int adjtimex_snapshot(struct timex * cpy) {
    static struct timex _prev_tx = { 0 };
    if (_prev_tx.time.tv_sec == 0) {
        if (_adjtimex_snapshot(&_prev_tx, "1st") < 0) {
            return -1;
        }
    }
    struct timex tx = { 0 };
    if (_adjtimex_snapshot(&tx, NULL) < 0) {
        return -1;
    }
    int change = timex_cmp(&_prev_tx, &tx);
    if (change) {
        slogdbg("adjtimex change detected ! (%d)\n", change);
        _prev_tx = tx;
    }
    if (cpy != NULL) {
        *cpy = tx;
    }
    return change;
}

///////////////////////////////////////////////////////////////////////////////
static long _adjtimex_get_tick(long * freq) {
    struct timex tx = { 0 };
    if (_adjtimex_snapshot(&tx, NULL) < TIME_OK) {
        return -1L;
    }
    if (NULL != freq) {
        *freq = tx.freq;
    }
    return tx.tick; 
}

static long _q16_to_ppb (long q16) {
    long double ppb_ld = (1e3L * (long double)q16) / 65536.0L;
    return (long)(roundl(ppb_ld));
}

long adjtimex_get_tick(long * freq_ppb) {
    long freq = 0;
    long tick = _adjtimex_get_tick(&freq);
    if (NULL != freq_ppb) {
        *freq_ppb = _q16_to_ppb(freq);
    }
    return tick; 
}

long adjtimex_get_freq(void) {
    long freq_ppb = 0;
    long tick = adjtimex_get_tick(&freq_ppb);
    return tick <= 0 ? 0 : freq_ppb;
}

///////////////////////////////////////////////////////////////////////////////
static long _ppb_to_q16 (long ppb) {
    return roundl( (65536.0l*ppb) / 1e3l );
}

static int _adjtimex_tick_freq(long tick_us, bool adj_tick,
                               long freq, bool adj_freq) {
    struct timex tx = { 0 };
    if (adj_tick || adj_freq) {
        tx.tick = _adjtimex_get_tick(&tx.freq);
        if (tx.tick <= 0) {
            return -1;
        }
        slogdbg("Current tick %ldus, current freq %+ldppb (%+ld).\n",
                tx.tick, _q16_to_ppb(tx.freq), tx.freq);
    }
    if (adj_tick) {
        tx.tick += tick_us;
    } else {
        tx.tick = tick_us;
    }
    if (adj_freq) {
        tx.freq += freq;
    } else {
        tx.freq = freq;
    }
    slogdbg("New tick %ldus, new freq %+ldppb (%+ld).\n",
            tx.tick, _q16_to_ppb(tx.freq), tx.freq);
    tx.modes = ADJ_TICK | ADJ_FREQUENCY;
    return _adjtimex_snapshot(&tx, "ADJ_TICK | ADJ_FREQ");
}

static int _adjtimex_set_freq(long freq) {
    return _adjtimex_tick_freq(0, true, freq, false);
}

int adjtimex_set_freq(long drift_ppb) {
    return _adjtimex_set_freq(_ppb_to_q16(drift_ppb));
}

int adjtimex_adj_freq(long drift_ppb) {
    return _adjtimex_tick_freq(0, true, _ppb_to_q16(drift_ppb), true);
}

int adjtimex_set_tick(long tick_us) {
    return _adjtimex_tick_freq(tick_us, false, 0, true);
}

int adjtimex_adj_tick(long tick_us) {
    return _adjtimex_tick_freq(tick_us, true, 0, true);
}

int adjtimex_adj_tick_freq(long tick_us, long freq_ppb) {
    return _adjtimex_tick_freq(tick_us, true, _ppb_to_q16(freq_ppb), true);
}

///////////////////////////////////////////////////////////////////////////////
// TODO not clear which one to use, but 1st try with ADJ_SETOFFSET does not
// play nice with negative offset...
// TODO nothing persistent in the timex struct... How is done the offset
// adjustment ? If done by skewing the clock, how to detect it ?
// TODO play with "adjtimex -s" (ADJ_OFFSET_SINGLESHOT) and "adjtimex -o"
// (ADJ_OFFSET) : on JETSON, singleshot works as expected, but ADJ_OFFSET
// do nothing ?!?
// -0.5s < offset_ns < +0.5s
int adjtimex_offset(long offset_ns) {
    struct timex tx = { 0 };
    tx.modes = ADJ_OFFSET | ADJ_NANO;
    tx.offset = offset_ns;
    slogcmt("Adjusting offset to %+ldns.\n", offset_ns);
    return _adjtimex_snapshot(&tx, "ADJ_OFFSET");
}

///////////////////////////////////////////////////////////////////////////////
/* FIXME FIXME FIXME FIXME FIXME FIXME FIXME
   from the man:
The  value  of  buf.time  is  the sum of its two fields, but the
              field buf.time.tv_usec must always be nonnegative.  The  follow‐
              ing  example  shows  how  to normalize a timeval with nanosecond
              resolution.

                  while (buf.time.tv_usec < 0) {
                      buf.time.tv_sec  -= 1;
                      buf.time.tv_usec += 1000000000;
                  }
*/   
int adjtimex_set_offset(long offset_ns) {
    struct timex tx = { 0 };
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    tx.time.tv_sec = 0;
    tx.time.tv_usec = offset_ns;
    while (tx.time.tv_usec < 0) {
        tx.time.tv_sec  -= 1;
        tx.time.tv_usec += 1000*1000*1000;
    }
    slogcmt("Setting offset to %+ld.%09ld\n", tx.time.tv_sec, tx.time.tv_usec);
    return _adjtimex_snapshot(&tx, "ADJ_SETOFFSET");
}

///////////////////////////////////////////////////////////////////////////////
int adjtimex_singleshot(long offset_ns) {
    // Exclusive mode
    struct timex tx = { 0 };
    tx.modes = ADJ_OFFSET_SINGLESHOT | ADJ_NANO;
    tx.offset = offset_ns;
    slogcmt("Setting offset to %+ldns\n", tx.offset);
    return _adjtimex_snapshot(&tx, "ADJ_OFFSET_SINGLESHOT");
}

///////////////////////////////////////////////////////////////////////////////
// no need to be root
long adjtimex_remaining(void) {
    // Exclusive mode
    struct timex tx = { 0 };
    tx.modes = ADJ_OFFSET_SS_READ | ADJ_NANO;
    if (_adjtimex_snapshot(&tx, "ADJ_OFFSET_SS_READ") < 0) {
        return 0;
    }
    return tx.offset;
}

///////////////////////////////////////////////////////////////////////////////
struct timex_limits_t const * TIMEX_LIMITS(void) {
    static struct timex_limits_t _LIMITS = { 0 };
    // backup current values
    long freq_back = 0;
    long tick_back = _adjtimex_get_tick (&freq_back);
    if (_LIMITS.tick_max_us == 0) {
        FILE * _stdout_back = adjtimex_stdout;
        adjtimex_stdout = NULL;
        long tick = 2*tick_back;
        while ((tick > 0) && (adjtimex_set_tick(tick) < 0)) {
            tick --; 
        }
        adjtimex_stdout = _stdout_back;
        if (tick > 0) {
            adjtimex_set_tick(tick_back);
            _LIMITS.tick_max_us = tick;
            slogdbg("Max tick %+ldus.\n", _LIMITS.tick_max_us);
        } else {
            slogout("%s\n", "Can not evaluates max tick value !");
        }
    }
    if (_LIMITS.tick_min_us == 0) {
        FILE * _stdout_back = adjtimex_stdout;
        adjtimex_stdout = NULL;
        long tick = 1;
        while ((tick < _LIMITS.tick_max_us) && (adjtimex_set_tick(tick) < 0)) {
            tick ++; 
        }
        adjtimex_stdout = _stdout_back;
        if (tick < _LIMITS.tick_max_us) {
            adjtimex_set_tick(tick_back);
            _LIMITS.tick_min_us = tick;
            slogdbg("Min tick %+ldus.\n", _LIMITS.tick_min_us);
        } else {
            slogout("%s\n", "Can not evaluates min tick value !");
        }
    }
    if (_LIMITS.freq_max_ppb == 0) {
        FILE * _stdout_back = adjtimex_stdout;
        adjtimex_stdout = NULL;
        long freq = 1000*65536;
        while ((freq > 0) && (_adjtimex_set_freq(freq) < 0)) {
            freq -= 65536; 
        }
        if (freq > 0) {
            _adjtimex_set_freq(freq_back);
            freq += 65536; 
            while ((freq > 0) && (_adjtimex_set_freq(freq) < 0)) {
                freq --; 
            }
            _adjtimex_set_freq(freq_back);
            _LIMITS.freq_max_ppb = _q16_to_ppb(freq);
            _LIMITS.freq_min_ppb = -_LIMITS.freq_max_ppb;
            adjtimex_stdout = _stdout_back;
            slogdbg("Max freq %+ldppb (%+ld).\n",
                    _LIMITS.freq_max_ppb, freq);
        } else {
            adjtimex_stdout = _stdout_back;
            slogout("%s\n", "Can not evaluates max freq value !");
        }
    }
    return &_LIMITS;
}


///////////////////////////////////////////////////////////////////////////////
int adjtimex_hardpps(bool freq, bool phase) {
    struct timex tx = { 0 };
    tx.modes = ADJ_STATUS;
    if (freq) {
        tx.status |= STA_PPSFREQ;
    }
    if (phase) {
        tx.status |= STA_PPSTIME;
    }
    return _adjtimex_snapshot(&tx, "HARDPPS");
}

///////////////////////////////////////////////////////////////////////////////
#ifdef ADJTIMEX_HELPER_MAIN
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include "timespec_helper.h"

#include "version.h"
#include "opt.h"

// TODO "-S, --status"
// TODO -R, -m, -e, -T
static struct option long_opts[] = {
    {"continuous", no_argument, NULL, 'C'},
    {"drift", required_argument, NULL, 'd'},
    {"frequency", required_argument, NULL, 'f'},
    {"offset", required_argument, NULL, 'o'},
    {"singleshot", required_argument, NULL, 's'},
    {"set-offset", required_argument, NULL, 'S'},
    {"tick", required_argument, NULL, 't'},
    {"print", no_argument, NULL, 'p'},
    {"quiet", no_argument, NULL, 'q'},
    {"ss-read", no_argument, NULL, 'r'},
    {"limits", no_argument, NULL, 'l'},
    {"verbose", no_argument, NULL, 'v'},
    /* show usage */
    {"help", no_argument, NULL, 'h'},
    {0,0,0,0}
};


int main(int argc, char ** argv) {

    adjtimex_stdout = stdout;
    adjtimex_stderr = NULL;
    adjtimex_stddbg = NULL;
    
    slogcmt("%s %s %s\n\n", argv[0], VERSION, COMMIT_DATE);

    char short_opts[255] = {0};
    longopts2shortopts (long_opts, short_opts);
    
    bool cont = false;
    bool disp = false;
    bool adj = false;
    bool set = false;
    bool ss_read = false;
    bool limits = false;
    long ppb = 0;
    long nso = 0;
    long nss = 0;
    long nsS = 0;
    long tick_us = 0;

    int opt;
    while ((opt = getopt_long(argc, argv,
                              short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
           case 'C':
           cont = true;
           break;
           case 'd':
           adj = true;
           ppb = atol(optarg);
           break;
           case 'f':
           set = true;
           ppb = atol(optarg);
           break;
           case 'o':
           nso = atol(optarg);
           break;
           case 's':
           nss= atol(optarg);
           break;
           case 'S':
           nsS= atol(optarg);
           break;
           case 't':
           tick_us = atol(optarg);
           break;
           case 'p':
           disp = true;
           break;
           case 'q':
           adjtimex_stdout = NULL;
           break;
           case 'r':
           ss_read = true;
           break;
           case 'l':
           limits = true;
           break;
           case 'v':
           if (adjtimex_stderr != NULL) {
               adjtimex_stddbg = adjtimex_stderr;
           } else {
               adjtimex_stderr = stderr;
           }
           break;
           default: /* '?' */
           print_usage(argv[0], NULL, long_opts, NULL, NULL);
           exit ((opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE);
        }
    }

    int ret = INT_MAX;
    if (adj) {
        ret = adjtimex_adj_freq(ppb);
    } 
    if (set) {
        ret = adjtimex_set_freq(ppb);
    }
    if (nso) ret = adjtimex_offset(nso);
    if (nss) ret = adjtimex_singleshot(nss);
    if (nsS) ret = adjtimex_set_offset(nsS);
    if (tick_us) ret = adjtimex_set_tick(tick_us);
    if (ret != INT_MAX) {
        fprintf(stdout, "adjtimex return \"%s\" (%d)\n",
                adjtimex_return_str(ret), ret);
        if (ret < 0) {
            fprintf(stdout, "\"%s\" (%d)\n", strerror(errno), errno);
            
        }
    }
    // TODO if argc == 1 ?
    if ((ppb == 0) && (nso == 0) && (nss == 0) && (nsS == 0) && (tick_us == 0)) 
    {
        disp = true;
    } else {
        ss_read = true;
    }
    if (ss_read) {
        fprintf(stdout, "SS_READ: %+ldns remaining\n", adjtimex_remaining());
    }
    if (limits) {
        fprintf(stdout, "%9ldus  <= tick <= %9ldus\n",
                TIMEX_LIMITS()->tick_min_us, TIMEX_LIMITS()->tick_max_us);
        fprintf(stdout, "%+9ldppb <= freq <= %+9ldppb\n",
                TIMEX_LIMITS()->freq_min_ppb, TIMEX_LIMITS()->freq_max_ppb);
    }
    if (disp) adjtimex_log(stdout);
    while (cont) {
        struct timex tx = {0};
        if (adjtimex_snapshot(&tx)) {
            fprintf(stdout, "##################\n");
            timex_flog(stdout, &tx);
            fprintf(stdout, "##################\n\n");
        }
    }
    return 0;
}
#endif // ADJTIMEX_HELPER_MAIN

