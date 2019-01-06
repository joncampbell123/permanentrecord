
#include <time.h>

extern time_t cut_interval;

extern time_t next_auto_cut;

void compute_auto_cut(void);
bool time_to_auto_cut(void);

