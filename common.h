
#include "config.h"

#if defined(WIN32)
# include <windows.h>
#endif

#include <string>
#include <vector>
#include <list>

#ifndef O_BINARY
#define O_BINARY (0)
#endif

typedef uint64_t monotonic_clock_t;

extern volatile int signal_to_die;

void sigma(int c);
uint32_t __leu24(const unsigned char *p);
int32_t __les24(const unsigned char *p);
int32_t __lesx24(uint32_t x);

