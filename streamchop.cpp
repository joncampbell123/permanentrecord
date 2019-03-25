
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <string>

std::string             opt_prefix;
std::string             opt_suffix;

unsigned int            replay_time_interval = 10;

time_t                  now = 0;
time_t                  cut_time = 0;
time_t                  start_time = 0;
time_t                  replay_mark_time = 0;

int                     p_fd = -1;
off_t                   p_fd_replay = -1;

int                     c_fd = -1;
std::string             c_fd_name;

enum {
    CUT_SEC,
    CUT_MIN,
    CUT_HOUR,
    CUT_DAY
};

int                     cut_amount = 3;
int                     cut_unit = CUT_HOUR;

std::string tm2string_fn(struct tm &t) {
    std::string ret;
    char tmp[512];

    sprintf(tmp,"%04u%02u%02u-%02u%02u%02u",
        t.tm_year+1900,
        t.tm_mon+1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec);
    ret = tmp;

    return ret;
}

std::string tm2string(struct tm &t) {
    std::string ret;
    char tmp[512];

    sprintf(tmp,"%04u-%02u-%02u %02u:%02u:%02u",
        t.tm_year+1900,
        t.tm_mon+1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec);
    ret = tmp;

    return ret;
}

std::string make_filename(void) { /* uses start_time */
    struct tm ct = *localtime(&start_time);
    std::string ret;

    ret  = opt_prefix;
    ret += tm2string_fn(ct);
    ret += opt_suffix;

    return ret;
}

time_t roundsec(struct tm &ct,const int sec) {
    time_t nt;

    assert(sec <= 60);
    assert(sec > 0);
    assert((60 % sec) == 0);/*period must be evenly divisble out of 60*/

    ct.tm_isdst = -1; // need to round according to calendar DST
    ct.tm_sec -= ct.tm_sec % sec;
    ct.tm_sec += sec;
    if (ct.tm_sec >= 60) {
        ct.tm_sec = 0;
        ct.tm_min++;
    }
    nt = mktime(&ct);
    if (nt == (time_t)(-1)) abort();
    return nt;
}

time_t roundmin(struct tm &ct,const int min) {
    time_t nt;

    assert(min <= 60);
    assert(min > 0);
    assert((60 % min) == 0);/*period must be evenly divisble out of 60*/

    ct.tm_isdst = -1; // need to round according to calendar DST
    ct.tm_sec  = 0;
    ct.tm_min -= ct.tm_min % min;
    ct.tm_min += min;
    if (ct.tm_min >= 60) {
        ct.tm_min = 0;
        ct.tm_hour++;
    }
    nt = mktime(&ct);
    if (nt == (time_t)(-1)) abort();
    return nt;
}

time_t roundhour(struct tm &ct,const int hour) {
    time_t nt;

    assert(hour <= 24);
    assert(hour > 0);
    assert((24 % hour) == 0);/*period must be evenly divisble out of 60*/

    ct.tm_isdst = -1; // need to round according to calendar DST
    ct.tm_sec   = 0;
    ct.tm_min   = 0;
    ct.tm_hour -= ct.tm_hour % hour;
    ct.tm_hour += hour;
    if (ct.tm_hour >= 24) {
        ct.tm_hour = 0;
        ct.tm_mday++;
    }
    nt = mktime(&ct);
    if (nt == (time_t)(-1)) abort();
    return nt;
}

time_t roundday(struct tm &ct,const int day) {
    time_t nt;

    assert(day > 0);

    ct.tm_isdst = -1; // need to round according to calendar DST
    ct.tm_sec   = 0;
    ct.tm_min   = 0;
    ct.tm_hour  = 0;

    ct.tm_mday--; // 1-based
    ct.tm_mday -= ct.tm_mday % day;
    ct.tm_mday += day;
    ct.tm_mday++;

    int p_mon = ct.tm_mon;

    nt = mktime(&ct);/*NTS: mktime() modifies the struct*/
    if (nt == (time_t)(-1)) abort();

    /* if the interval crosses into the next month, then recompute to cut at the start of the new month */
    if (p_mon != ct.tm_mon) {
        ct.tm_mday = 1;
        return roundday(ct,day);
    }

    return nt;
}

static void update_cut_time(const time_t s) {
    struct tm ct = *localtime(&s);
    time_t nt;

    printf("Now:      %s\n",tm2string(ct).c_str());

    if (cut_unit == CUT_SEC)
        nt = roundsec(ct,cut_amount);
    else if (cut_unit == CUT_MIN)
        nt = roundmin(ct,cut_amount);
    else if (cut_unit == CUT_HOUR)
        nt = roundhour(ct,cut_amount);
    else if (cut_unit == CUT_DAY)
        nt = roundday(ct,cut_amount);
    else
        abort();

    cut_time = nt;
    assert(s < cut_time);

    {
        ct = *localtime(&nt);
        printf("Cut date: %s\n",tm2string(ct).c_str());
    }

    replay_mark_time = cut_time - (time_t)replay_time_interval;
    {
        time_t t = replay_mark_time;
        ct = *localtime(&t);
        printf("Markdate: %s\n",tm2string(ct).c_str());
    }
}

bool open_c_fd(void) {
    if (c_fd < 0) {
        c_fd_name = make_filename();
        c_fd = open(c_fd_name.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644); /* for archival reasons DO NOT overwrite existing files */
        if (c_fd < 0) return false;
    }

    return true;
}

void close_c_fd(void) {
    if (c_fd >= 0) {
        close(c_fd);
        c_fd = -1;
    }
}

void close_p_fd(void) {
    if (p_fd >= 0) {
        close(p_fd);
        p_fd = -1;
    }
}

static void help(void) {
    fprintf(stderr,"-p prefix\n");
    fprintf(stderr,"-s suffix\n");
    fprintf(stderr,"-ca interval amount\n");
    fprintf(stderr,"-cu interval unit (second, minute, hour, day)\n");
}

int main(int argc,char **argv) {
    {
        char *a;
        int i=1;

        while (i < argc) {
            a = argv[i++];

            if (*a == '-') {
                do { a++; } while (*a == '-');

                if (!strcmp(a,"h")) {
                    help();
                    return 1;
                }
                else if (!strcmp(a,"ca")) {
                    a = argv[i++];
                    cut_amount = atoi(a);
                }
                else if (!strcmp(a,"cu")) {
                    a = argv[i++];
                    if (!strcasecmp(a,"second"))
                        cut_unit = CUT_SEC;
                    else if (!strcasecmp(a,"minute"))
                        cut_unit = CUT_MIN;
                    else if (!strcasecmp(a,"hour"))
                        cut_unit = CUT_HOUR;
                    else if (!strcasecmp(a,"day"))
                        cut_unit = CUT_DAY;
                    else
                        abort();
                }
                else if (!strcmp(a,"p")) {
                    opt_prefix = argv[i++];
                }
                else if (!strcmp(a,"s")) {
                    opt_suffix = argv[i++];
                }
                else {
                    fprintf(stderr,"Unknown switch %s\n",a);
                    help();
                    return 1;
                }
            }
            else {
                fprintf(stderr,"Unknown arg %s\n",a);
                return 1;
            }
        }
    }

    /* make sure interval is valid */
    if (cut_unit == CUT_SEC ||
        cut_unit == CUT_MIN) {
        if (cut_amount <= 0 || cut_amount >= 60 || (60 % cut_amount) != 0) {
            fprintf(stderr,"Invalid interval. Must evenly divide from 60 (try 1, 5, 10, 15, 30...)\n");
            return 1;
        }
    }
    else if (cut_unit == CUT_HOUR) {
        if (cut_amount <= 0 || cut_amount >= 24 || (24 % cut_amount) != 0) {
            fprintf(stderr,"Invalid interval. Must evenly divide from 24 (try 1, 2, 3, 6...)\n");
            return 1;
        }
    }
    else if (cut_unit == CUT_DAY) {
        if (cut_amount <= 0 || cut_amount >= 31) {
            fprintf(stderr,"Invalid interval. Try 1 through 30\n");
            return 1;
        }
    }

    start_time = now = time(NULL);
    update_cut_time(start_time);

    if (!open_c_fd()) return 1;

    while (1) {

    }

    close_c_fd();
    close_p_fd();
    return 0;
}

