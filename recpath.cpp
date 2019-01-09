
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
#include "wavstruc.h"
#include "wavwrite.h"
#include "recpath.h"

#include "as_alsa.h"

std::string make_recording_path_now(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm == NULL) return std::string();

    std::string rec;
    char tmp[128];

    rec = "PERMREC";
    if (mkdir(rec.c_str(),0755) < 0) {
        if (errno != EEXIST)
            return std::string();
    }

    /* tm->tm_year + 1900 = current year
     * tm->tm_mon + 1     = current month (1=January)
     * tm->tm_mday        = current day of the month */
    sprintf(tmp,"/%04u%02u%02u",tm->tm_year + 1900,tm->tm_mon + 1,tm->tm_mday);
    rec += tmp;
    if (mkdir(rec.c_str(),0755) < 0) {
        if (errno != EEXIST)
            return std::string();
    }

    /* caller must add file extension needed */
    sprintf(tmp,"/TM%02u%02u%02u",tm->tm_hour,tm->tm_min,tm->tm_sec);
    rec += tmp;

    return rec;
}

