
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "config.h"

typedef uint64_t monotonic_clock_t;

/* clock source pick */
#if defined(C_CLOCK_GETTIME)
static inline monotonic_clock_t monotonic_clock_rate(void) {
    return 1000ul;
}

monotonic_clock_t monotonic_clock_gettime();
#endif

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

monotonic_clock_t monotonic_clock() {
#if defined(C_CLOCK_GETTIME)
    return monotonic_clock_gettime();
#else
    return 0;
#endif
}

static void help(void) {
    fprintf(stderr,"-h --help      Help text\n");
}

static int parse_argv(int argc,char **argv) {
    char *a;
    int i=1;

    while (i < argc) {
        a = argv[i++];
        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return 1;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg\n");
            return 1;
        }
    }

    return 0;
}

int main(int argc,char **argv) {
    if (parse_argv(argc,argv))
        return 1;

    return 0;
}

