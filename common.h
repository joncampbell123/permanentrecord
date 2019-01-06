
#include "config.h"

#include <string>
#include <vector>
#include <list>

#ifndef O_BINARY
#define O_BINARY (0)
#endif

typedef uint64_t monotonic_clock_t;

extern volatile int signal_to_die;

void sigma(int c);
 
