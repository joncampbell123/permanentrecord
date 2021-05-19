
#if !defined(_WIN32)/*NOT for Windows*/

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
#include <list>
#include <map>

#ifndef O_BINARY
#define O_BINARY (0)
#endif

using namespace std;

static void chomp(char *s) {
    char *e = s + strlen(s) - 1;
    while (e >= s && (*e == '\n' || *e == '\r')) *e-- = 0;
}

static bool valid_url(const string url) {
    if (url.find_first_of('$') != string::npos) return false;
    if (url.find_first_of('\\') != string::npos) return false;
    if (url.find_first_of('\'') != string::npos) return false;
    if (url.find_first_of('\"') != string::npos) return false;

    return true;
}

int download_m3u8(const string dpath,const string url) {
    if (!valid_url(url))
        return 1;

    string cmd = string("wget -nv -t 10 --show-progress -O '") + dpath + "' '" + url + "'";
    int x = system(cmd.c_str());

    if (x != 0) {
        fprintf(stderr,"Download failed\n");
        return 1;
    }

    return 0;
}

int download_m3u8_fragment(const string dpath,const string url) {
    if (!valid_url(url))
        return 1;

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

again:
    /* skip the ID3v2 tag */
    lseek(fd,ofs,SEEK_SET);
    if (read(fd,buf,10) == 10) {
        if (memcmp(buf,"ID3",3) == 0 && buf[3] >= 1 && buf[3] <= 4 &&
            ((buf[6] | buf[7] | buf[8] | buf[9]) & 0x80) == 0) {
            /* ID3 tag length is byte count following header */
            ofs += (off_t)10 + ((off_t)((buf[6] << 21ul) | (buf[7] << 14ul) | (buf[8] << 7ul) | (buf[9] << 0ul)));
            fprintf(stderr,"ID3 tag ends at %lu\n",(unsigned long)ofs);
            goto again; /* some streams *cough cough CNN* have two consecutive ID3v2 tags at the start of some fragments! */
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
        int                     bandwidth = -1;         // EXT-X-STREAM-INF
        string                  codecs;                 // EXT-X-STREAM-INF
        double                  duration = -1;          // EXT-INF
        string                  title;                  // EXT-INF
        bool                    discontinuity = false;  // EXT-X-DISCONTINUITY
        bool                    is_stream_inf = false;  // EXT-X-STREAM-INF
        int                     resolution_width = -1;  // EXT-X-STREAM-INF
        int                     resolution_height = -1; // EXT-X-STREAM-INF
        string                  program_date_time;      // EXT-X-PROGRAM-DATE-TIME
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
        int                     parse_file(const string path,const string url);
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
    fprintf(fp,"  resolution=%d x %d\n",resolution_width,resolution_height);
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

int M3U8::parse_file(const string path,const string url) {
    char tmp[16384],*s;
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
            else if (!strcmp(name,"EXT-X-PROGRAM-DATE-TIME"))
                ent.program_date_time = value;
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
                    else if (!strcmp(iname,"RESOLUTION")) {
                        /* ex: RESOLUTION=640x360 */
                        if (isdigit(*ivalue)) {
                            ent.resolution_width = (int)strtol(ivalue,&ivalue,10);
                            if (*ivalue == 'x') ivalue++;
                        }
                        if (isdigit(*ivalue)) {
                            ent.resolution_height = (int)strtol(ivalue,&ivalue,10);
                        }
                    }
                }
            }
        }
        else if (*s != 0) {
            // url

            if (!strncmp(s,"http://",7) || !strncmp(s,"https://",8)) {
                ent.url = s;
            }
            // TODO: Urls that are absolute in domain, start with /
            else {
                ent.url = url;
                {
                    int i = (int)ent.url.length();
                    while (i >= 0 && !(ent.url[(size_t)i] == '/')) i--;
                    i++;
                    assert(i >= 0 && size_t(i) <= ent.url.length());
                    ent.url = ent.url.substr(0,(size_t)i);
                }
                ent.url += s;
            }

            m3u8list.push_back(ent);
            ent = M3U8Entry();
        }
    }

    fclose(fp);
    return 0;
}

class DownloadTracking {
    public:
        string                      program_date_time;
        double                      duration = 0;
        time_t                      expiration = 0;
        bool                        done = false;
        bool                        seen = false;
        bool                        gone = false;
};

map<string,DownloadTracking>        downloaded;
list<string>                        download_todo;
string                              downloading;

string              translate_mode;
M3U8                main_m3u8;
string              main_url;
M3U8                stream_m3u8;
string              stream_url;
bool                giveup = false;
int                 want_bandwidth = -1;
bool                hls_files = false;
string              hls_files_suffix;
string              hls_frag_exec;
string              m3u8_file;

static void help() {
    fprintf(stderr,"hls_audio [options] <m3u8 url>\n");
    fprintf(stderr,"  -b <n>            Select stream by bandwidth\n");
    fprintf(stderr,"  -xlate <n>        Translation mode (default: none)\n");
    fprintf(stderr,"                    none                Do not translate (ID3 tags are stripped)\n");
    fprintf(stderr,"                    ts2aac              Convert .ts to .aac, for HLS audio feeds (requires FFMPEG)\n");
    fprintf(stderr,"  -hlsfiles <suf>   Record HLS fragments to individual files with given file suffix\n");
    fprintf(stderr,"  -hlsfragexec <x>  Run command x every HLS fragment (minimum 5 second interval)\n");
    fprintf(stderr,"  -m3u8 <path>      With -hlsfiles, append each new fragment to this .m3u8 file\n");
}

static int parse_argv(int argc,char **argv) {
    int i,nswi=0;
    char *a;

    i=1;
    while (i < argc) {
        a = argv[i++];

        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h")) {
                help();
                return 1;
            }
            else if (!strcmp(a,"b")) {
                a = argv[i++];
                if (a == NULL) return 1;
                want_bandwidth = atoi(a);
            }
            else if (!strcmp(a,"xlate")) {
                a = argv[i++];
                if (a == NULL) return 1;
                translate_mode = a;
            }
            else if (!strcmp(a,"hlsfragexec")) {
                a = argv[i++];
                if (a == NULL) return 1;

                hls_frag_exec = a;
            }
            else if (!strcmp(a,"hlsfiles")) {
                a = argv[i++];
                if (a == NULL) return 1;

                if (strstr(a,"/") != NULL) {
                    fprintf(stderr,"Forward slashes not allowed in suffix\n");
                    return 1;
                }

                hls_files_suffix = a;
                hls_files = true;
            }
            else if (!strcmp(a,"m3u8")) {
                a = argv[i++];
                if (a == NULL) return 1;
                m3u8_file = a;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
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

    if (translate_mode == "none")
        translate_mode.clear();

    if (main_url.empty()) {
        fprintf(stderr,"Need url\n");
        return 1;
    }

    return 0;
}

int main(int argc,char **argv) {
    time_t now,next_m3u8_dl = 0;
    time_t next_frag_exec = 0;
    bool has_m3u8 = false;
    FILE *m3u8_fp = NULL;

    if (parse_argv(argc,argv))
        return 1;

    /* the main URL is probably something that points to other m3u8s with info about bandwidth */
    {
        unsigned int tryc=0;

        while (download_m3u8("tmp.main.m3u8",main_url)) {
            if (++tryc > 5) return 1;
            sleep(2);
        }

        if (main_m3u8.parse_file("tmp.main.m3u8",main_url)) {
            fprintf(stderr,"Failed to parse M3U8\n");
            return 1;
        }
        main_m3u8.dump();
        stream_url.clear();

        if (stream_url.empty() && want_bandwidth > 0) {
            int sel_bw = -1;

            for (auto i = main_m3u8.m3u8list.begin();i != main_m3u8.m3u8list.end();i++) {
                if ((*i).is_stream_inf && !(*i).url.empty() && (*i).bandwidth > 0 &&
                    (*i).bandwidth <= want_bandwidth && (*i).bandwidth > sel_bw) {
                    sel_bw = (*i).bandwidth;
                    stream_url = (*i).url;
                }
            }
        }

        if (stream_url.empty() && !main_m3u8.m3u8list.empty()) {
            auto i = main_m3u8.m3u8list.front();
            if (i.is_stream_inf && !i.url.empty()) {
                stream_url = i.url;
            }
        }

        if (stream_url.empty())
            stream_url = main_url;

        fprintf(stderr,"Chosen stream URL: %s\n",stream_url.c_str());
    }

    if (!m3u8_file.empty()) {
        struct stat st;

        if (stat(m3u8_file.c_str(),&st) == 0 && S_ISREG(st.st_mode))
            has_m3u8 = true; // and therefore append to
 
        if (download_m3u8("tmp.stream.m3u8",stream_url) == 0) {
            stream_m3u8 = M3U8();
            if (stream_m3u8.parse_file("tmp.stream.m3u8",stream_url) == 0) {
            }
        }

        if (has_m3u8) {
            m3u8_fp = fopen(m3u8_file.c_str(),"a");
        }
        else {
            FILE *fp = fopen(m3u8_file.c_str(),"w");
            if (fp != NULL) {
                fprintf(fp,"#EXTM3U\n");
                fprintf(fp,"#EXT-X-PLAYLIST-TYPE:EVENT\n");
                fprintf(fp,"#EXT-X-VERSION:4\n");
                fprintf(fp,"#EXT-X-MEDIA-SEQUENCE:0\n");
                if (stream_m3u8.targetduration > 0) fprintf(fp,"#EXT-X-TARGETDURATION:%u\n",(int)(stream_m3u8.targetduration+0.5));
                fflush(fp);
                m3u8_fp = fp;
            }
        }
    }

    while (!giveup) {
        now = time(NULL);
        if (now >= next_m3u8_dl) {
            next_m3u8_dl = now + 5;
            if (download_m3u8("tmp.stream.m3u8",stream_url) == 0) {
                stream_m3u8 = M3U8();
                if (stream_m3u8.parse_file("tmp.stream.m3u8",stream_url) == 0) {
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
                    for (auto i=stream_m3u8.m3u8list.begin();i!=stream_m3u8.m3u8list.end();i++) {
                        if (!((*i).url.empty())) {
                            if (!downloaded[(*i).url].gone) {
                                downloaded[(*i).url].program_date_time = (*i).program_date_time;
                                downloaded[(*i).url].duration = (*i).duration;
                                download_todo.push_back((*i).url);
                            }
                        }
                    }
                    {
                        auto i = downloaded.begin();
                        while (i != downloaded.end() && i->second.gone && now >= i->second.expiration) {
//                          fprintf(stderr,"Flushing gone download '%s'\n",i->first.c_str());
                            downloaded.erase(i);
                            i = downloaded.begin();
                        }
                    }
                }
            }
        }

        int downloadcount = 0;

        while (downloadcount == 0) {
            if (downloading.empty() && !download_todo.empty()) {
                downloading = download_todo.front();
                download_todo.pop_front();
            }

            if (!downloading.empty()) {
                if (downloaded[downloading].done || downloaded[downloading].gone) {
                    downloading.clear();
                }
                else if (download_m3u8_fragment("tmp.fragment.bin",downloading) == 0) {
                    fprintf(stderr,"Fragment '%s' obtained\n",downloading.c_str());

                    if (hls_files) {
                        if (!translate_mode.empty()) {
                            fprintf(stderr,"Unsupported translate mode with HLS files mode\n");
                            break;
                        }
                        else {
                            string finalpath;

                            {
                                struct stat st;
                                char tmp[128];
                                struct tm *tm;
                                time_t now;
                                int count=0;

                                now = time(NULL);
                                tm = localtime(&now);
                                assert(tm != NULL);

                                while (1) {
                                    snprintf(tmp,sizeof(tmp),"%04u%02u%02u-%02u%02u%02u-%03u-",
                                        tm->tm_year+1900,
                                        tm->tm_mon+1,
                                        tm->tm_mday,
                                        tm->tm_hour,
                                        tm->tm_min,
                                        tm->tm_sec,
                                        count++);

                                    finalpath = tmp;

                                    {
                                        const char *s = downloading.c_str();
                                        const char *r = strrchr(s,'/');
                                        if (r != NULL) {
                                            r++;
                                            while (*r != 0) {
                                                if (*r < 33 || *r == '?' || *r == '#' || *r == '/')
                                                    finalpath += "_";
                                                else
                                                    finalpath += *r;

                                                r++;
                                            }
                                            finalpath += '-';
                                        }
                                    }

                                    finalpath += hls_files_suffix;

                                    if (stat(finalpath.c_str(),&st) == 0) {
                                    }
                                    else {
                                        break;
                                    }
                                }
                            }

                            if (rename("tmp.fragment.bin",finalpath.c_str())) {
                                fprintf(stderr,"WARNING: Rename failed\n");
                            }
                            else {
                                if (m3u8_fp != NULL) {
                                    if (!downloaded[downloading].program_date_time.empty())
                                        fprintf(m3u8_fp,"#EXT-X-PROGRAM-DATE-TIME:%s\n",downloaded[downloading].program_date_time.c_str());

                                    fprintf(m3u8_fp,"#EXTINF:%.3f,\n",downloaded[downloading].duration);
                                    fprintf(m3u8_fp,"%s\n",finalpath.c_str());
                                    fflush(m3u8_fp);
                                }
                            }

                            if (now >= next_frag_exec) {
                                next_frag_exec += 5;
                                if (next_frag_exec < now) next_frag_exec = now + 5;

                                if (!hls_frag_exec.empty())
                                    system(hls_frag_exec.c_str());
                            }
                        }
                    }
                    else {
                        if (!isatty(1)) {
                            if (translate_mode == "ts2aac") {
                                /* WARNING: If FFMPEG emits any warnings about "MPEG TS PES packet corrupt", stop using this
                                 *          mode, it means the HLS source is letting ADTS packets span HLS fragments, and the
                                 *          warning means that the ADTS audio frame that got cut in half is getting discarded.
                                 *
                                 *          A future commit to this project will provide it's own MPEG TS demux that would
                                 *          extract and provide every single byte provided in the fragment, regardless of ADTS
                                 *          boundaries, so that such streams are preserved perfectly. */
                                unlink("tmp.fragment.aac");

                                int x = system("ffmpeg -f mpegts -i tmp.fragment.bin -acodec copy -y -f adts tmp.fragment.aac");
                                if (x != 0) fprintf(stderr,"Warning: FFMPEG failed to convert file\n");

                                int w = file_to_stdout("tmp.fragment.aac");
                                if (w == 0) {
                                    fprintf(stderr,"File to stdout indicates EOF\n");
                                    break;
                                }
                            }
                            else {
                                int w = file_to_stdout("tmp.fragment.bin");
                                if (w == 0) {
                                    fprintf(stderr,"File to stdout indicates EOF\n");
                                    break;
                                }
                            }
                        }
                    }

                    downloaded[downloading].done = true;

                    downloadcount++;
                    downloading.clear();
                }
                else {
                    break;
                }
            }
            else {
                break;
            }
        }

        sleep(1);
    }

    if (m3u8_fp != NULL) {
        fclose(m3u8_fp);
        m3u8_fp = NULL;
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

