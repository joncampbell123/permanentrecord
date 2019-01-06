
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <endian.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "monclock.h"

/* clock source pick */
#if defined(C_CLOCK_GETTIME)
monotonic_clock_t monotonic_clock_gettime() {
    struct timespec t;

    if (clock_gettime(CLOCK_MONOTONIC,&t) >= 0) {
        return
            ((monotonic_clock_t)t.tv_sec  * (monotonic_clock_t)1000ul) +        /* seconds to milliseconds */
            ((monotonic_clock_t)t.tv_nsec / (monotonic_clock_t)1000000ul);      /* nanoseconds to millseconds */
    }

    return 0;
}
#endif

monotonic_clock_t monotonic_clock_rate(void) {
#if defined(C_CLOCK_GETTIME)
    return 1000ul;
#else
    return 1000ul;
#endif
}

monotonic_clock_t monotonic_clock() {
#if defined(C_CLOCK_GETTIME)
    return monotonic_clock_gettime();
#else
    return 0;
#endif
}

