
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include <string>

using namespace std;

string              main_url;
string              stream_url;

static int parse_argv(int argc,char **argv) {
    int i,nswi=0;
    char *a;

    i=1;
    while (i < argc) {
        a = argv[i++];

        if (*a == '-') {
            do { a++; } while (*a == '-');

            return 1;
        }
        else {
            switch (nswi++) {
                case 0:
                    main_url = a;
                    break;
                default:
                    return 1;
            }
        }
    }

    if (main_url.empty()) {
        fprintf(stderr,"Need url\n");
        return 1;
    }

    return 0;
}

int download_m3u8(const string dpath,const string url) {
    string cmd = string("wget -t 10 --show-progress -O '") + dpath + "' '" + url + "'";
    int x = system(cmd.c_str());

    if (x != 0) {
        fprintf(stderr,"Download failed\n");
        return 1;
    }

    return 0;
}

int main(int argc,char **argv) {
    if (parse_argv(argc,argv))
        return 1;

    /* the main URL is probably something that points to other m3u8s with info about bandwidth */
    {
        unsigned int tryc=0;

        while (download_m3u8("tmp.main.m3u8",main_url)) {
            if (++tryc > 5) return 1;
            sleep(2);
        }
    }

    return 0;
}

