
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <string>
#include <vector>
#include <list>

#include "config.h"

#if defined(HAVE_ALSA)
# define ALSA_PCM_NEW_HW_PARAMS_API
# include <alsa/asoundlib.h>
#endif

typedef uint64_t monotonic_clock_t;

/* clock source pick */
#if defined(C_CLOCK_GETTIME)
static inline monotonic_clock_t monotonic_clock_rate(void) {
    return 1000ul;
}

monotonic_clock_t monotonic_clock_gettime();
#endif

#if defined(C_CLOCK_GETTIME)
monotonic_clock_t monotonic_clock_gettime() {
    struct timespec t;

    if (clock_gettime(CLOCK_MONOTONIC,&t) >= 0) {
        return
            ((monotonic_clock_t)t.tv_sec  * (monotonic_clock_t)1000ul) +        /* seconds to milliseconds */
            ((monotonic_clock_t)t.tv_nsec / (monotonic_clock_t)1000000ul);      /* nanoseconds to millseconds */
    }

    return 0;
}
#endif

monotonic_clock_t monotonic_clock() {
#if defined(C_CLOCK_GETTIME)
    return monotonic_clock_gettime();
#else
    return 0;
#endif
}

enum {
    AFMT_PCMU=1,
    AFMT_PCMS=2
};

struct AudioFormat {
    uint16_t            format_tag;
    uint32_t            sample_rate;
    uint8_t             channels;
    uint8_t             bits_per_sample;
};

struct AudioOptionPair {
    std::string         name,value;
};

struct AudioDevicePair {
    std::string         name,desc;
};

class AudioSource {
public:
    AudioSource() { };
    virtual ~AudioSource() { }
public:
    virtual int EnumOptions(std::vector<AudioOptionPair> &names) { (void)names; return -ENOSPC; }
    virtual int SetOption(const char *name,const char *value) { (void)name; (void)value; return -ENOSPC; }
    virtual int SelectDevice(const char *str) { (void)str; return -ENOSPC; }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) { (void)names; return -ENOSPC; }
    virtual int SetFormat(const struct AudioFormat &fmt) { (void)fmt; return -ENOSPC; }
    virtual int GetFormat(struct AudioFormat &fmt) { (void)fmt; return -ENOSPC; }
    virtual int QueryFormat(struct AudioFormat &fmt) { (void)fmt; return -ENOSPC; }
    virtual int Open(void) { return -ENOSPC; }
    virtual int Close(void) { return -ENOSPC; }
    virtual bool IsOpen(void) { return false; }
    virtual int Read(void *buffer,unsigned int bytes) { (void)buffer; (void)bytes; return -ENOSPC; }
    virtual const char *GetSourceName(void) { return "baseclass"; }
    virtual const char *GetDeviceName(void) { return ""; }
public:
    virtual unsigned int GetBytesPerFrame(void) { return 0; }
    virtual unsigned int GetSamplesPerFrame(void) { return 0; }
};

#if defined(HAVE_ALSA)
class AudioSourceALSA : public AudioSource {
public:
    AudioSourceALSA() : alsa_pcm(NULL), alsa_pcm_hw_params(NULL) { }
    virtual ~AudioSourceALSA() { alsa_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            alsa_device_string = str;
            return 0;
        }

        return -EBUSY;
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
        void **hints,**n;

        names.clear();

        if (snd_device_name_hint(-1,"pcm",&hints) == 0) {
            n = hints;
            while (*n != NULL) {
                char *name = snd_device_name_get_hint(*n, "NAME");
                char *desc = snd_device_name_get_hint(*n, "DESC");
                char *ioid = snd_device_name_get_hint(*n, "IOID");

                if (name != NULL) {
                    bool hasinput = false;

                    if (ioid == NULL) {
                        /* ALSA will return NULL to mean it can do input and output */
                        hasinput = true;
                    }
                    else if (!strcmp(ioid,"Input")) {
                        hasinput = true;
                    }

                    if (hasinput) {
                        /* "desc" can have newlines, remove them */
                        {
                            char *s = desc;
                            while (*s != 0) {
                                if (*s == '\n' || *s == '\r') *s = ' ';
                                s++;
                            }
                        }

                        AudioDevicePair p;
                        p.name = name;
                        p.desc = desc!=NULL?desc:"";

                        names.push_back(p);
                    }
                }

                if (name) free(name);
                if (desc) free(desc);
                if (ioid) free(ioid);
                n++;
            }

            snd_device_name_free_hint(hints);
        }

        return 0;
    }
    virtual bool IsOpen(void) { return (alsa_pcm != NULL); }
    virtual const char *GetSourceName(void) { return "ALSA"; }
    virtual const char *GetDeviceName(void) { return alsa_device_string.c_str(); }
public:
    virtual unsigned int GetBytesPerFrame(void) { return 0; }
    virtual unsigned int GetSamplesPerFrame(void) { return 0; }
private:
    snd_pcm_t*			        alsa_pcm;
    snd_pcm_hw_params_t*		alsa_pcm_hw_params;
    std::string                 alsa_device_string;
private:
    void alsa_force_close(void) {
        /* TODO */
    }
};
#endif

static void help(void) {
    fprintf(stderr,"-h --help      Help text\n");
}

static int parse_argv(int argc,char **argv) {
    char *a;
    int i=1;

    while (i < argc) {
        a = argv[i++];
        if (*a == '-') {
            do { a++; } while (*a == '-');

            if (!strcmp(a,"h") || !strcmp(a,"help")) {
                help();
                return 1;
            }
            else {
                fprintf(stderr,"Unknown switch %s\n",a);
                return 1;
            }
        }
        else {
            fprintf(stderr,"Unexpected arg\n");
            return 1;
        }
    }

    return 0;
}

int main(int argc,char **argv) {
    if (parse_argv(argc,argv))
        return 1;

    AudioSourceALSA alsa;

    {
        std::vector<AudioDevicePair> l;
        alsa.EnumDevices(l);

        printf("Devices:\n");
        for (auto i=l.begin();i!=l.end();i++) {
            printf(" \"%s\"   %s\n",
                (*i).name.c_str(),
                (*i).desc.c_str());
        }
    }

#if defined(HAVE_ALSA)
    snd_config_update_free_global();
#endif

    return 0;
}

