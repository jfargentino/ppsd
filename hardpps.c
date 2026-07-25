#include "adjtimex_helper.h"
#include "pps_helper.h"
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    char const * pps_path = "/dev/pps0";
    bool pps_capture_assert = false;
    struct pps_t * pps = pps_open(pps_path, pps_capture_assert);
    if (NULL == pps) {
        // TODO using the rough estimate to set the clock and
        // evaluate the drift ?
        (void)fprintf(stderr, "No PPS available !\n");
        exit (EXIT_FAILURE);
    }
    if (pps_hardpps(pps) < 0) {
    }
    pps_close(pps);
    //bool freq = true;
    //bool phase = false;
    //adjtimex_hardpps(freq, phase);
    exit (EXIT_SUCCESS);
}

