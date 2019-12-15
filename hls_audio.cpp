
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include <string>
#include <vector>
#include <map>

using namespace std;

static void chomp(char *s) {
    char *e = s + strlen(s) - 1;
    while (e >= s && (*e == '\n' || *e == '\r')) *e-- = 0;
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

class M3U8Entry {
    public:
        string                  url;
        int                     bandwidth;              // EXT-X-STREAM-INF
        string                  codecs;                 // EXT-X-STREAM-INF
        double                  duration = -1;          // EXT-INF
        string                  title;                  // EXT-INF
        bool                    discontinuity = false;  // EXT-X-DISCONTINUITY
        bool                    is_stream_inf = false;  // EXT-X-STREAM-INF
    public:
        void                    dump(FILE *fp=NULL);
};

class M3U8 {
    public:
        bool                    is_m3u8 = false;        // EXTM3U
        int                     version = -1;           // EXT-X-VERSION
        double                  targetduration = -1;    // EXT-X-TARGETDURATION
        signed long long        media_sequence = -1ll;  // EXT-X-MEDIA-SEQUENCE
        vector<M3U8Entry>       m3u8list;
    public:
        int                     parse_file(const string path);
        void                    dump(FILE *fp=NULL);
};

void M3U8Entry::dump(FILE *fp) {
    if (fp == NULL)
        fp = stderr;

    fprintf(fp,"M3U8Entry:\n");
    fprintf(fp,"  url='%s'\n",url.c_str());
    fprintf(fp,"  bandwidth=%d\n",bandwidth);
    fprintf(fp,"  codecs='%s'\n",codecs.c_str());
    fprintf(fp,"  duration=%.3f\n",duration);
    fprintf(fp,"  title='%s'\n",title.c_str());
    fprintf(fp,"  discontinuity=%u\n",discontinuity?1:0);
}

void M3U8::dump(FILE *fp) {
    if (fp == NULL)
        fp = stderr;

    fprintf(fp,"M3U8 dump: is_m3u8=%u version=%d targetduration=%.3f media_sequence=%lld\n",
        is_m3u8?1:0,
        version,
        targetduration,
        media_sequence);

    fprintf(stderr,"M3U8 list {\n");
    for (auto i=m3u8list.begin();i!=m3u8list.end();i++) (*i).dump(fp);
    fprintf(stderr,"}\n");
}

int M3U8::parse_file(const string path) {
    char tmp[1024],*s;
    M3U8Entry ent;
    FILE *fp;

    fp = fopen(path.c_str(),"r");
    if (fp == NULL) return 1;

    while (!feof(fp) && !ferror(fp)) {
        if (fgets(tmp,sizeof(tmp),fp) == NULL) break;
        chomp(tmp);
        s=tmp;

        if (*s == '#') {
            s++;

            char *name = s;
            char *value = strchr(s,':');
            if (value != NULL)
                *value++ = 0;
            else
                value = s + strlen(s);

            if (!strcmp(name,"EXTM3U"))
                is_m3u8 = true;
            else if (!strcmp(name,"EXT-X-VERSION"))
                version = atoi(value);
            else if (!strcmp(name,"EXT-X-TARGETDURATION"))
                targetduration = atof(value);
            else if (!strcmp(name,"EXT-X-MEDIA-SEQUENCE"))
                media_sequence = strtoll(value,NULL,10);
            else if (!strcmp(name,"EXT-X-DISCONTINUITY"))
                ent.discontinuity = true;
            else if (!strcmp(name,"EXTINF")) {
                /* value, title */
                if (isdigit(*value)) {
                    ent.duration = strtof(value,&value);
                    if (*value == ',') value++;
                    while (*value == ' ') value++;
                }

                ent.title = value;
            }
            else if (!strcmp(name,"EXT-X-STREAM-INF")) {
                ent.is_stream_inf = true;
                /* name=value,name=value, ... */
                while (*value != 0) {
                    if (*value == ' ' || *value == '\t') {
                        value++; // unlikely, just in case
                        continue;
                    }

                    char *iname = value;
                    while (*value && !(*value == ',' || *value == '=')) value++;
                    if (*value == '=') *value++ = 0;
                    char *ivalue = value;
                    if (*value == '\"') {
                        ivalue = ++value;
                        while (*value && !(*value == '\"')) value++;
                        if (*value == '\"') *value++ = 0;
                    }
                    else {
                        while (*value && !(*value == ',')) value++;
                    }
                    if (*value == ',') *value++ = 0;

                    if (!strcmp(iname,"BANDWIDTH"))
                        ent.bandwidth = atoi(ivalue);
                    else if (!strcmp(iname,"CODECS"))
                        ent.codecs = ivalue;
                }
            }
        }
        else if (*s != 0) {
            // url
            ent.url = s;
            m3u8list.push_back(ent);
            ent = M3U8Entry();
        }
    }

    fclose(fp);
    return 0;
}

M3U8                main_m3u8;
string              main_url;
M3U8                stream_m3u8;
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

        if (main_m3u8.parse_file("tmp.main.m3u8")) {
            fprintf(stderr,"Failed to parse M3U8\n");
            return 1;
        }
        main_m3u8.dump();
    }

    return 0;
}

