
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
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

#include "common.h"
#include "monclock.h"
#include "aufmt.h"
#include "aufmtui.h"
#include "audev.h"
#include "ausrc.h"
#include "ausrcls.h"
#include "dbfs.h"
#include "autocut.h"

time_t cut_interval = (time_t)60 * (time_t)60; // 1 hour, on the hour

time_t next_auto_cut = 0;

void compute_auto_cut(void) {
    time_t now = time(NULL);
    struct tm *tmnow = localtime(&now);
    if (tmnow == NULL) return;
    struct tm tmday = *tmnow;
    tmday.tm_hour = 0;
    tmday.tm_min = 0;
    tmday.tm_sec = 0;
    time_t daystart = mktime(&tmday);
    if (daystart == (time_t)-1) return;

    if (now < daystart) {
        fprintf(stderr,"mktime() problem with start of day\n");
        abort();
    }

    time_t delta = now - daystart;
    delta -= delta % cut_interval;
    delta += cut_interval;

    next_auto_cut = daystart + delta;
}

bool time_to_auto_cut(void) {
    time_t now = time(NULL);

    if (next_auto_cut != (time_t)0 && now >= next_auto_cut)
        return true;

    return false;
}

