
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include <string>
#include <vector>
#include <map>

#ifndef O_BINARY
#define O_BINARY (0)
#endif

using namespace std;

static void chomp(char *s) {
    char *e = s + strlen(s) - 1;
    while (e >= s && (*e == '\n' || *e == '\r')) *e-- = 0;
}

int download_m3u8(const string dpath,const string url) {
    string cmd = string("wget -nv -t 10 --show-progress -O '") + dpath + "' '" + url + "'";
    int x = system(cmd.c_str());

    if (x != 0) {
        fprintf(stderr,"Download failed\n");
        return 1;
    }

    return 0;
}

int download_m3u8_fragment(const string dpath,const string url) {
    string cmd = string("wget -nv -t 15 --show-progress -O '") + dpath + "' '" + url + "'";
    int x = system(cmd.c_str());

    if (x != 0) {
        fprintf(stderr,"Download failed\n");
        return 1;
    }

    return 0;
}

int file_to_stdout(const string spath) {
    unsigned char buf[4096];
    off_t ofs = 0;
    int r,w,rt;
    int c=0;
    int fd;

    fd = open(spath.c_str(),O_RDONLY | O_BINARY);
    if (fd < 0) return -1;

    /* skip the ID3v2 tag */
    lseek(fd,ofs,SEEK_SET);
    if (read(fd,buf,10) == 10) {
        if (memcmp(buf,"ID3",3) == 0 && buf[3] >= 1 && buf[3] <= 4 &&
            ((buf[6] | buf[7] | buf[8] | buf[9]) & 0x80) == 0) {
            ofs = (off_t)((buf[6] << 21ul) | (buf[7] << 14ul) | (buf[8] << 7ul) | (buf[9] << 0ul));
            fprintf(stderr,"ID3 tag size %lu\n",(unsigned long)ofs);
        }
    }

    lseek(fd,ofs,SEEK_SET);
    while ((r=(int)read(fd,buf,sizeof(buf))) > 0) {
        w=0;
        while (w < r) {
            rt=(int)write(1/*STDOUT*/,buf+w,size_t(r-w));
            if (rt == 0) {
                c = 0;
                break;
            }
            else if (rt < 0) {
                c = -1;
                break;
            }
            else {
                c = rt;
                w += c;
                assert(w <= r);
            }
        }

        if (c <= 0)
            break;
    }

    close(fd);
    return c;
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

class DownloadTracking {
    public:
        time_t                      expiration = 0;
        bool                        done = false;
        bool                        seen = false;
        bool                        gone = false;
};

map<string,DownloadTracking>        downloaded;
string                              downloading;
bool                                downloading_eof = false;

M3U8                main_m3u8;
string              main_url;
M3U8                stream_m3u8;
string              stream_url;
bool                giveup = false;

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
    time_t now,next_m3u8_dl = 0;

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
//      main_m3u8.dump();
        stream_url = main_url;

        /* main stream or pick one within? TODO: Let the user choose. */
        if (!main_m3u8.m3u8list.empty()) {
            auto i = main_m3u8.m3u8list.front();
            if (i.is_stream_inf && !i.url.empty()) {
                stream_url = i.url;
            }
        }

        fprintf(stderr,"Chosen stream URL: %s\n",stream_url.c_str());
    }

    while (!giveup) {
        now = time(NULL);
        if (now >= next_m3u8_dl) {
            next_m3u8_dl = now + 5;
            if (download_m3u8("tmp.stream.m3u8",stream_url) == 0) {
                stream_m3u8 = M3U8();
                if (stream_m3u8.parse_file("tmp.stream.m3u8") == 0) {
                    for (auto i=downloaded.begin();i!=downloaded.end();i++) {
                        if (!i->second.gone) {
                            i->second.expiration = now + 60;
                            i->second.gone = true;
                        }
                    }
                    for (auto i=stream_m3u8.m3u8list.begin();i!=stream_m3u8.m3u8list.end();i++) {
                        if (!((*i).url.empty())) {
                            downloaded[(*i).url].gone = false;
                            downloaded[(*i).url].seen = true;
                        }
                    }

                    if (!downloading.empty()) {
                        auto i=downloaded.find(downloading);
                        if (i != downloaded.end()) {
                            if (i->second.gone) {
                                fprintf(stderr,"Fragment '%s' disappeared before we could finish downloading\n",downloading.c_str());
                                downloading.clear();
                            }
                        }
                    }
                    {
                        auto i = downloaded.begin();
                        while (i != downloaded.end() && i->second.gone && now >= i->second.expiration) {
                            fprintf(stderr,"Flushing gone download '%s'\n",i->first.c_str());
                            downloaded.erase(i);
                            i = downloaded.begin();
                        }
                    }
                    if (downloading.empty()) {
                        if (!stream_m3u8.m3u8list.empty()) {
                            if (downloading_eof) {
                                downloading = stream_m3u8.m3u8list.back().url;
                            }
                            else {
                                downloading = stream_m3u8.m3u8list.front().url;
                            }

                            if (!downloading.empty())
                                fprintf(stderr,"Starting download with '%s'\n",downloading.c_str());
                        }
                    }
                }
            }
        }

        if (!downloading.empty()) {
            bool do_next = false;

            if (downloaded[downloading].done) {
                do_next = true;
            }
            else if (download_m3u8_fragment("tmp.fragment.bin",downloading) == 0) {
                fprintf(stderr,"Fragment '%s' obtained\n",downloading.c_str());

                if (!isatty(1)) {
                    int w = file_to_stdout("tmp.fragment.bin");
                    if (w == 0) {
                        fprintf(stderr,"File to stdout indicates EOF\n");
                        break;
                    }
                }

                do_next = true;
            }
            else {
                if (!downloading.empty()) {
                    if (downloaded[downloading].gone) {
                        do_next = true;
                    }
                }
            }

            if (do_next) {
                int count = -1;
                string new_url;
                {
                    auto i = stream_m3u8.m3u8list.begin();
                    while (i != stream_m3u8.m3u8list.end()) {
                        if (!(*i).url.empty()) {
                            if (!downloaded[(*i).url].gone &&
                                 downloaded[(*i).url].seen) {
                                if ((*i).url == downloading) {
                                    downloaded[(*i).url].done = true;
                                    count = 0;
                                }
                                else if (!downloaded[(*i).url].done) {
                                    if (count < 0) count = 0;
                                    else if (count++ >= 0) {
                                        new_url = (*i).url;
                                        break;
                                    }
                                }
                            }
                        }

                        i++;
                    }

                    if (i == stream_m3u8.m3u8list.end())
                        downloading_eof = true;
                }

                downloading = new_url;
                if (downloading_eof) {
                    assert(downloading.empty());
                    fprintf(stderr,"Waiting for next fragment\n");
                }
                else {
                    fprintf(stderr,"Downloading next fragment '%s'\n",new_url.c_str());
                }
            }
        }

        sleep(1);
    }

    return 0;
}

