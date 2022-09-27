
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

// FIXME: Byte order?
uint32_t __leu24(const unsigned char *p) {
    return ((uint32_t)p[0]) +
           ((uint32_t)p[1] << (uint32_t)8u) +
	   ((uint32_t)p[2] << (uint32_t)16);
}

// FIXME: Byte order?
int32_t __les24(const unsigned char *p) {
	const uint32_t r = __leu24(p);
	return (int32_t)r - (int32_t)((r & 0x800000u) << 1u);
}

int32_t __lesx24(uint32_t x) {
	return (int32_t)(x & 0xFFFFFFu) - (int32_t)((x & 0x800000u) << 1u);
}

