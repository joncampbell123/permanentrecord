
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
#include "monclock.h"
#include "aufmt.h"
#include "aufmtui.h"
#include "audev.h"
#include "ausrc.h"
#include "ausrcls.h"

std::string ui_print_format(AudioFormat &fmt) {
    std::string ret;
    char tmp[64];

    switch (fmt.format_tag) {
        case AFMT_PCMU:
            ret += "pcm-unsigned";
            break;
        case AFMT_PCMS:
            ret += "pcm-signed";
            break;
        case 0:
            ret += "none";
            break;
        default:
            ret += "?";
            break;
    };

    sprintf(tmp," %luHz",(unsigned long)fmt.sample_rate);
    ret += tmp;

    sprintf(tmp," %u-ch",(unsigned int)fmt.channels);
    ret += tmp;

    sprintf(tmp," %u-bit",(unsigned int)fmt.bits_per_sample);
    ret += tmp;

    return ret;
}

