
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_MSC_VER)
# include <io.h>
#else
# include <unistd.h>
#endif
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "common.h"

volatile int signal_to_die = 0;

void sigma(int c) {
    (void)c;

    signal_to_die++;
}

