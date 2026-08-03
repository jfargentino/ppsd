#ifndef DRIFT_FILE_H
#define DRIFT_FILE_H

#include <time.h>

int drift_fread (char const * drift_file_path,
                 struct timespec * ref,
                 long long * offset0_ns,
                 long long * offset1_ns,
                 long long * drift_adjust_ppb,
                 long long * drift_total_ppb);

int drift_fwrite (char const * drift_file_path,
                  struct timespec const * ref,
                  long long offset0_ns,
                  long long offset1_ns,
                  long long drift_adjust_ppb,
                  long long drift_total_ppb);

#endif

