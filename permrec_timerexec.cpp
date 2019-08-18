
#include <signal.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <string>
#include <vector>

using namespace std;

volatile int DIE = 0;

static void sigma(int x) {
    (void)x;
    if (++DIE >= 10) abort();
}

time_t                  now = 0;

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

static void help(void) {
    fprintf(stderr,"permrec_timerexec [options] <command> [command args]\n");
    fprintf(stderr," --daily <dspec>-<dspec>\n");
    fprintf(stderr,"\n");
    fprintf(stderr," dspec: h[:m[:s]]      h=hour(0-23) m=minute s=second\n");
}

enum class TimeRangeType {
    Daily = 0
};

struct TimeSpec {
    int                 hour = -1;
    int                 minute = -1;
    int                 second = -1;
    time_t              timeval = 0;

    void default_begin(void) {
        if (hour < 0) hour = 0;
        if (minute < 0) minute = 0;
        if (second < 0) second = 0;
    }
    void default_end(void) {
        if (hour < 0) hour = 0;
        if (minute < 0) minute = 0;
        if (second < 0) second = 0;
    }
    time_t time(void) const {
        return timeval;
    }
    time_t time(const time_t _now,const TimeRangeType _type) {
        struct tm tm;

        (void)_type; // unused for now

        tm = *localtime(&_now);

        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;

        return (timeval=mktime(&tm));
    }
    bool parse_string(char * &s) {
        hour = minute = second = -1;

        /* h[:m[:s]] */
        if (!isdigit(*s)) return false;

        hour = (int)strtol(s,&s,10);
        if (hour < 0 || hour > 23) return false;

        if (*s == 0 || *s == '-') return true;
        if (*s != ':') return false;
        s++;

        minute = (int)strtol(s,&s,10);
        if (minute < 0 || minute > 59) return false;

        if (*s == 0 || *s == '-') return true;
        if (*s != ':') return false;
        s++;

        second = (int)strtol(s,&s,10);
        if (second < 0 || second > 59) return false;

        if (*s == 0 || *s == '-') return true;

        return false;
    }
};

struct TimeRange {
    TimeRangeType       type = TimeRangeType::Daily;
    TimeSpec            start,end;

    bool inverted(void) const {
        return start.time() >= end.time();
    }
    void default_fill(void) {
        start.default_begin();
        end.default_end();
    }
    time_t begin_time(void) const {
        return start.time();
    }
    time_t end_time(void) const {
        return end.time();
    }
    time_t begin_time(const time_t _now) {
        return start.time(_now,type);
    }
    time_t end_time(const time_t _now) {
        return end.time(_now,type);
    }
    bool parse_string(char * &s) {
        if (!start.parse_string(/*&*/s))
            return false;

        if (*s != '-') return false;
        s++;

        if (!end.parse_string(/*&*/s))
            return false;

        return true;
    }
    void wrap_correct(void) {
        if (inverted()) {
            struct tm tm = *localtime(&end.timeval);

            tm.tm_mday++;

            end.timeval = mktime(&tm);

            assert(!inverted());
        }
    }
    bool timeval_defined(void) const {
        return begin_time() != 0 && end_time() != 0;
    }
    bool expired(const time_t _now) const {
        return timeval_defined() && _now > end_time();
    }
};

vector<TimeRange>   time_ranges;
vector<string>      program_args;

bool time_to_run(const time_t _now,const TimeRange &range) {
    if (_now >= range.begin_time() && _now < range.end_time())
        return true;

    return false;
}

bool time_to_run(void) {
    now = time(NULL);

    for (auto &range : time_ranges)
        if (time_to_run(now,range))
            return true;

    return false;
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
                else if (!strcmp(a,"daily")) {
                    a = argv[i++];
                    if (a == NULL) return 1;

                    TimeRange r;
                    if (!r.parse_string(a)) {
                        fprintf(stderr,"daily range parse fail\n");
                        return 1;
                    }

                    time_ranges.push_back(move(r));
                }
                else {
                    fprintf(stderr,"Unknown switch %s\n",a);
                    help();
                    return 1;
                }
            }
            else {
                i--;//undo i++
                break;
            }
        }

        while (i < argc)
            program_args.push_back(argv[i++]);
    }

    if (program_args.empty()) {
        fprintf(stderr,"Program to run not specified\n");
        return 1;
    }
    if (time_ranges.empty()) {
        fprintf(stderr,"Time ranges required\n");
        return 1;
    }

    signal(SIGINT,sigma);
    signal(SIGQUIT,sigma);
    signal(SIGTERM,sigma);

    while (!DIE) {
        usleep(250000);
    }

    return 0;
}

