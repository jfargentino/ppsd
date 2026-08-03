#include "drift_file.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define STDOUT stdout
#define STDERR stdout
#include "slog.h"

static int _drift_fread (char const * drift_file_path,
                         struct timespec * ref,
                         long long * offset0_ns,
                         long long * offset1_ns,
                         long long * drift_adjust_ppb,
                         long long * drift_total_ppb,
                         bool cmt) {
    if (NULL == drift_file_path) {
        return 0;
    }
    FILE * drift_fd = fopen(drift_file_path, "rt");
    if (NULL == drift_fd) {
        if (cmt) {
            slogcmt("Drift file \"%s\" does not exist.\n", drift_file_path);
        }
        return 0;
    }
    if (cmt) {
        slogerr("%c", '\n');
        slogcmt("Reading dritf file \"%s\":\n", drift_file_path);
    }
    rewind(drift_fd);
    int lines_nb = 0;
    char line[256] /* = {'\0'} */;
    while (fgets(line, 256, drift_fd) != NULL) {
        if (line[0] == '#') {
            if (cmt) { slogcmt("%s", line); }
        } else {
            lines_nb ++;
            // TODO check that fscanf return >= 5
            (void)sscanf(line,
                         "%ld.%ld; %lld; %lld; %lld; %lld\n",
                         &ref->tv_sec, &ref->tv_nsec,
                         offset0_ns,
                         offset1_ns,
                         drift_adjust_ppb,
                         drift_total_ppb);
        }
    }
    (void)fclose(drift_fd);
    if ((cmt) && (lines_nb > 0)) {
        slogcmt("%ld.%09ld; %+lld; %+lld; %+lld; %+lld\n",
                ref->tv_sec, ref->tv_nsec,
                *offset0_ns,
                *offset1_ns,
                *drift_adjust_ppb,
                *drift_total_ppb);
    }
    return lines_nb;
}

int drift_fread (char const * drift_file_path,
                 struct timespec * ref,
                 long long * offset0_ns,
                 long long * offset1_ns,
                 long long * drift_adjust_ppb,
                 long long * drift_total_ppb) {
    return _drift_fread(drift_file_path,
                        ref,
                        offset0_ns,
                        offset1_ns,
                        drift_adjust_ppb,
                        drift_total_ppb,
                        true);
}

int drift_fwrite (char const * drift_file_path,
                  struct timespec const * ref,
                  long long offset0_ns,
                  long long offset1_ns,
                  long long drift_adjust_ppb,
                  long long drift_total_ppb) {
    if (NULL == drift_file_path) {
        return 0;
    }

    struct timespec dum0;
    long long dum1;
    long long dum2;
    long long dum3;
    long long dum4;
    bool print_hdr = ( _drift_fread (drift_file_path,
                                     &dum0,
                                     &dum1,
                                     &dum2,
                                     &dum3,
                                     &dum4,
                                     false) > 0 ) ? false : true;

    FILE* drift_fd = fopen(drift_file_path, "at");
    if (NULL == drift_fd) {
        slogerr("Can not create drift file \"%s\": %s\n",
                drift_file_path,
                strerror(errno));
        return -1;
    }
    slogcmt("Writing in drift file \"%s\":\n", drift_file_path);
    if (print_hdr) {
        (void)fseek(drift_fd, 0, SEEK_SET);
        (void)fprintf(drift_fd,
                      "#Tref; "
                      "offset0 (ns); "
                      "offset1 (ns); "
                      "drift (ppb); "
                      "correction (ppb)\n");
        slogcmt("%s\n",
                "Tref; "
                "offset0 (ns); "
                "offset1 (ns); "
                "drift (ppb); "
                "correction (ppb)");
    }
    (void)fseek(drift_fd, 0, SEEK_END);
    (void)fprintf(drift_fd,
                  "%ld.%09ld; %+9lld; %+9lld; %+6lld; %+lld\n",
                  ref->tv_sec, ref->tv_nsec,
                  offset0_ns,
                  offset1_ns,
                  drift_adjust_ppb,
                  drift_total_ppb);
    (void)fclose(drift_fd);
    slogcmt("%ld.%09ld; %+9lld; %+9lld; %+6lld; %+lld\n",
            ref->tv_sec, ref->tv_nsec,
            offset0_ns,
            offset1_ns,
            drift_adjust_ppb,
            drift_total_ppb);
    return 0;
}

