
#if !defined(_WIN32)/*NOT for Windows*/

#include <sys/types.h>
#include <sys/wait.h>
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

bool                    print_timerange = true;
bool                    exit_if_terminate = false;
bool                    exit_if_none = false;
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
    fprintf(stderr," --daily [wspec]<dspec>-<dspec>\n");
    fprintf(stderr," -x --exit-if-none      Exit when none of the time ranges are in effect.\n");
    fprintf(stderr," --npt                  Don't print time calc\n");
    fprintf(stderr,"\n");
    fprintf(stderr," wspec: w[sun,mon,tue,wed,thu,fri,sat]   which days of the week to allow execution\n");
    fprintf(stderr," wspec: w-[sun,mon,tue,wed,thu,fri,sat]  which days of the week NOT to allow execution\n");
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

        tm.tm_sec = second;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_isdst = -1;

        return (timeval=mktime(&tm));
    }
    bool parse_string(char * &s) {
        hour = minute = second = -1;

        /* h[:m[:s]] */
        if (!isdigit(*s)) return false;

        hour = (int)strtol(s,&s,10);
        if (hour < 0 || hour > 23) return false;
        /* allow 'a' and 'p' suffixes for the hour */
        if (*s == 'a') {
            if (hour > 12) return false;
            hour %= 12;
            s++;
        }
        else if (*s == 'p') {
            if (hour > 12) return false;
            hour %= 12;
            hour += 12;
            s++;
        }

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
    bool                weekly_enable[7] = {/*Sun*/true,/*Mon*/true,/*Tue*/true,/*Wed*/true,/*Thu*/true,/*Fri*/true,/*Sat*/true};
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
    void recalc_time(const time_t _now) {
        begin_time(_now);
        end_time(_now);
        wrap_correct();

        if (expired(_now) || !unit_enabled(start.timeval)) {
            int patience = 10000;

            do {
                assert((--patience) > 0); /* should never happen unless somehow all days of the week were zeroed, which should not happen either! */
                start.timeval = next_unit_adv(start.timeval);
                end.timeval = next_unit_adv(end.timeval);
            } while (!unit_enabled(start.timeval));
            assert(!expired(_now));
        }
    }
    bool unit_enabled(const time_t _now) {
        if (type == TimeRangeType::Daily) {
            const int dow = day_of_week(_now);
            if (dow >= 0 && dow <= 6)
                return weekly_enable[dow];
        }

        return true;
    }
    bool parse_string(char * &s) {
        for (unsigned int i=0;i < 7;i++) weekly_enable[i] = true;

        /* week spec? allow daily schedule that only runs on specific days of the week */
        if (*s == 'w') {
            bool exclude = false;

            s++; /* skip 'w' */
            /* w[Sun,Mon,Tue,Wed,Thu,Fri,Sat] = only the days listed */
            /* w-[Sat,Sun] = all days except listed */

            if (*s == '-') {
                s++; /* skip '-' */
                exclude = true;
            }

            /* unless excluding, mark all days disabled  */
            if (!exclude) {
                for (unsigned int i=0;i < 7;i++) weekly_enable[i] = false;
            }

            /* start parsing */
            if (*s == '[') {
                s++;

                do {
                    if (*s == 0) return false; /* end of string (invalid) */
                    if (*s == ']') { s++; break; } /* end of spec */

                    /* match day of week */
                    int dow = -1;
                    if (!strncmp(s,"sun",3)) {
                        dow = 0;
                        s += 3;
                    }
                    else if (!strncmp(s,"mon",3)) {
                        dow = 1;
                        s += 3;
                    }
                    else if (!strncmp(s,"tue",3)) {
                        dow = 2;
                        s += 3;
                    }
                    else if (!strncmp(s,"wed",3)) {
                        dow = 3;
                        s += 3;
                    }
                    else if (!strncmp(s,"thu",3)) {
                        dow = 4;
                        s += 3;
                    }
                    else if (!strncmp(s,"fri",3)) {
                        dow = 5;
                        s += 3;
                    }
                    else if (!strncmp(s,"sat",3)) {
                        dow = 6;
                        s += 3;
                    }
                    else {
                        return false;
                    }

                    if (exclude)
                        weekly_enable[dow] = false;
                    else
                        weekly_enable[dow] = true;

                    if (*s == ']') { s++; break; }
                    else if (*s == ',') { s++; continue; }
                    else return false;
                } while (1);
            }
            else {
                return false;
            }

            /* bug check */
            {
                int c=0;
                for (int i=0;i < 7;i++)
                    c += (weekly_enable[i] ? 1 : 0);

                if (c == 0)
                    return false;
            }

            /* allow space between week spec and timespec */
            while (*s == ' ') s++;
        }

        if (!start.parse_string(/*&*/s))
            return false;

        if (*s != '-') return false;
        s++;

        if (!end.parse_string(/*&*/s))
            return false;

        default_fill();
        return true;
    }
    time_t next_unit_adv(const time_t _t) const {
        struct tm tm = *localtime(&_t);

        // FIXME: Daily only
        tm.tm_mday++;

        return mktime(&tm);
    }
    void wrap_correct(void) {
        if (inverted()) {
            end.timeval = next_unit_adv(end.timeval);
            assert(!inverted());
        }
    }
    bool timeval_defined(void) const {
        return begin_time() != 0 && end_time() != 0;
    }
    bool expired(const time_t _now) const {
        return timeval_defined() && _now > end_time();
    }
    bool time_to_run(const time_t _now) const {
        return (_now >= begin_time() && _now < end_time());
    }
    int day_of_week(const time_t now) const {
        if (timeval_defined()) {
            struct tm tm = *localtime(&now);
            return tm.tm_wday;
        }

        return -1;
    }
};

vector<TimeRange>   time_ranges;
vector<string>      program_args;

bool time_to_run(const time_t _now) {
    for (auto &range : time_ranges)
        if (range.time_to_run(_now))
            return true;

    return false;
}

pid_t               process_group = (pid_t)-1;

void check_process(void) {
    int status;
    pid_t pid;

    if (process_group > (pid_t)0) {
        pid = waitpid(-process_group,&status,WNOHANG);
        if (pid > (pid_t)0)
            fprintf(stderr,"Process %d terminated (group %d)\n",(int)pid,(int)process_group);

        if (kill(-process_group,0) != 0) {
            if (errno == ESRCH) {
                fprintf(stderr,"Process group finished\n");
                process_group = (pid_t)-1;
            }
            else {
                fprintf(stderr,"Process group scan failed? %s\n",strerror(errno));
            }
        }
    }
}

bool want_stop = false;
time_t stop_timeout = 0;
unsigned int run_counter = 0;

void force_kill(void) {
    if (process_group > (pid_t)0)
        kill(-process_group,SIGKILL);
}

bool running(void) {
    if (want_stop && (process_group > (pid_t)0)) {
        if (time(NULL) >= stop_timeout) {
            fprintf(stderr,"Stop timeout: Force terminating process group %d\n",(int)process_group);
            force_kill();
            stop_timeout += 5;
        }
    }

    check_process();
    return process_group > (pid_t)0;
}

void run(void) {
    if (!running()) {
        pid_t pid;

        want_stop = false;

        pid = fork();
        if (pid < 0) return;

        if (pid == 0) {/*child*/
            char **arg = new char*[program_args.size()+size_t(1)];

            {
                size_t i=0;

                for (;i < program_args.size();i++)
                    arg[i] = (char*)program_args[i].c_str();

                arg[i] = NULL;
            }

            setpgrp(); /* we are the process group leader */

            {
                int fd = open("/dev/null",O_RDONLY);
                dup2(fd,0); /* over STDIN */
                close(fd);
            }

            execv(arg[0],arg);

            fprintf(stderr,"Failed to exec %s\n",arg[0]);

            delete[] arg;
            _exit(1);
        }
        else {
            run_counter++;
            process_group = pid;
        }
    }
}

void stop(void) {
    if (!want_stop) {
        want_stop = true;
        stop_timeout = time(NULL) + 10; /* YOU HAVE 10 SECONDS TO COMPLY */
    }

    if (running()) {
        kill(-process_group,SIGINT);
        kill(-process_group,SIGTERM);
    }
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
                else if (!strcmp(a,"npt")) {
                    print_timerange = false;
                }
                else if (!strcmp(a,"exit-if-terminate")) {
                    exit_if_terminate = true;
                }
                else if (!strcmp(a,"x") || !strcmp(a,"exit-if-none")) {
                    exit_if_none = true;
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
        time_t now = time(NULL);

        for (auto &ev : time_ranges) {
            if (!ev.timeval_defined() || ev.expired(now)) {
                time_t tmp;

                ev.recalc_time(now);

                if (print_timerange) {
                    fprintf(stderr,"Recalc time range:\n");

                    tmp = ev.begin_time();
                    fprintf(stderr,"  Start: %s",ctime(&tmp));

                    tmp = ev.end_time();
                    fprintf(stderr,"    End: %s",ctime(&tmp));
                }
            }
        }

        if (time_to_run(now)) {
            if (!running()) {
                if (run_counter > 0 && exit_if_terminate) break;
                fprintf(stderr,"Running program\n");
                run();
            }
        }
        else {
            if (running()) {
                fprintf(stderr,"Stopping program\n");
                stop();

                while (running() && !DIE)
                    usleep(250000);

                fprintf(stderr,"Program stopped\n");
            }

            if (exit_if_none) break;
        }

        usleep(500000);
    }

    if (running()) {
        fprintf(stderr,"Stopping program\n");
        stop();

        while (running() && !DIE)
            usleep(250000);

        fprintf(stderr,"Program stopped\n");
    }

    return 0;
}
#else /*WIN32*/
#include <stdio.h>
int main() {
	fprintf(stderr,"Not for Windows\n");
	return 1;
}
#endif /*WIN32*/

