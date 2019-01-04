
#include <stdio.h>
#include <assert.h>
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
};

#if defined(HAVE_ALSA)
class AudioSourceALSA : public AudioSource {
public:
    AudioSourceALSA() : alsa_pcm(NULL), alsa_pcm_hw_params(NULL), alsa_device_string("default"), bytes_per_frame(0), samples_per_frame(0), isUserOpen(false) {
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourceALSA() { alsa_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            std::string sel = (str != NULL && *str != 0) ? str : "default";

            if (alsa_device_string != sel) {
                alsa_close();
                alsa_device_string = sel;
            }

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
    virtual bool IsOpen(void) { return (alsa_pcm != NULL) && isUserOpen; }
    virtual const char *GetSourceName(void) { return "ALSA"; }
    virtual const char *GetDeviceName(void) { return alsa_device_string.c_str(); }

    virtual int Open(void) {
        if (!IsOpen()) {
            if (chosen_format.format_tag == 0) {
                if (init_format() < 0)
                    return -EINVAL;
            }
            if (!alsa_open())
                return false;
            if (!alsa_apply_format(chosen_format))
                return false;

            if (snd_pcm_prepare(alsa_pcm) < 0)
                return false;
            if (snd_pcm_start(alsa_pcm) < 0)
                return false;

            isUserOpen = true;
        }

        return true;
    }
    virtual int Close(void) {
        isUserOpen = false;
        alsa_close();
        return 0;
    }

    virtual int SetFormat(const struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return false;

        if (!alsa_open())
            return -ENODEV;

        AudioFormat tmp = fmt;
        if (!alsa_apply_format(/*&*/tmp)) {
            alsa_close();
            return -EINVAL;
        }

        chosen_format = tmp;
        alsa_close();
        return 0;
    }
    virtual int init_format(void) {
        if (!alsa_open())
            return -ENODEV;
        if (!alsa_apply_format(/*&*/chosen_format)) {
            alsa_close();
            return -EINVAL;
        }
        alsa_close();
        return 0;
    }
    virtual int GetFormat(struct AudioFormat &fmt) {
        if (chosen_format.format_tag == 0) {
            if (init_format() < 0)
                return -EINVAL;
        }

        fmt = chosen_format;
        return 0;
    }
    virtual int QueryFormat(struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        if (!alsa_open())
            return -ENODEV;

        if (!alsa_apply_format(/*&*/fmt)) {
            alsa_close();
            return -EINVAL;
        }

        alsa_close();
        return 0;
    }
private:
    snd_pcm_t*			        alsa_pcm;
    snd_pcm_hw_params_t*		alsa_pcm_hw_params;
    std::string                 alsa_device_string;
    AudioFormat                 chosen_format;
    unsigned int                bytes_per_frame;
    unsigned int                samples_per_frame;
    bool                        isUserOpen;
private:
    bool format_is_valid(const AudioFormat &fmt) {
        if (fmt.format_tag == AFMT_PCMU || fmt.format_tag == AFMT_PCMS) {
            if (fmt.sample_rate == 0)
                return false;
            if (fmt.channels == 0)
                return false;
            if (fmt.bits_per_sample == 0)
                return false;

            return true;
        }

        return false;
    }
    bool alsa_apply_format(AudioFormat &fmt) {
        if (alsa_pcm != NULL && alsa_pcm_hw_params != NULL) {
            snd_pcm_format_t alsafmt;
            unsigned int alsa_channels = 0;
            unsigned int alsa_rate = 0;
            int	alsa_rate_dir = 0;

            if (fmt.format_tag != 0) {
                if (fmt.format_tag == AFMT_PCMU) {
                    if (fmt.bits_per_sample == 8)
                        alsafmt = SND_PCM_FORMAT_U8;
                    else if (fmt.bits_per_sample == 16)
                        alsafmt = SND_PCM_FORMAT_U16;
                    else if (fmt.bits_per_sample == 24)
                        alsafmt = SND_PCM_FORMAT_U24;
                    else if (fmt.bits_per_sample == 32)
                        alsafmt = SND_PCM_FORMAT_U32;
                    else
                        alsafmt = SND_PCM_FORMAT_U16;
                }
                else if (fmt.format_tag == AFMT_PCMS) {
                    if (fmt.bits_per_sample == 8)
                        alsafmt = SND_PCM_FORMAT_S8;
                    else if (fmt.bits_per_sample == 16)
                        alsafmt = SND_PCM_FORMAT_S16;
                    else if (fmt.bits_per_sample == 24)
                        alsafmt = SND_PCM_FORMAT_S24;
                    else if (fmt.bits_per_sample == 32)
                        alsafmt = SND_PCM_FORMAT_S32;
                    else
                        alsafmt = SND_PCM_FORMAT_S16;
                }
                else {
                    alsafmt = SND_PCM_FORMAT_S16;
                }

                if (snd_pcm_hw_params_test_format(alsa_pcm,alsa_pcm_hw_params,alsafmt) < 0) {
                    switch (alsafmt) {
                        case SND_PCM_FORMAT_U8:
                            alsafmt = SND_PCM_FORMAT_S8;
                            break;
                        case SND_PCM_FORMAT_U16:
                            alsafmt = SND_PCM_FORMAT_S16;
                            break;
                        case SND_PCM_FORMAT_U24:
                            alsafmt = SND_PCM_FORMAT_S24;
                            break;
                        case SND_PCM_FORMAT_U32:
                            alsafmt = SND_PCM_FORMAT_S32;
                            break;
                        default:
                            break;
                    };

                    if (snd_pcm_hw_params_test_format(alsa_pcm,alsa_pcm_hw_params,alsafmt) < 0) {
                        switch (alsafmt) {
                            case SND_PCM_FORMAT_S8:
                                alsafmt = SND_PCM_FORMAT_S16;
                                break;
                            case SND_PCM_FORMAT_S16:
                                alsafmt = SND_PCM_FORMAT_S8;
                                break;
                            case SND_PCM_FORMAT_S24:
                                alsafmt = SND_PCM_FORMAT_S16;
                                break;
                            case SND_PCM_FORMAT_S32:
                                alsafmt = SND_PCM_FORMAT_S16;
                                break;
                            case SND_PCM_FORMAT_U8:
                                alsafmt = SND_PCM_FORMAT_U16;
                                break;
                            case SND_PCM_FORMAT_U16:
                                alsafmt = SND_PCM_FORMAT_U8;
                                break;
                            case SND_PCM_FORMAT_U24:
                                alsafmt = SND_PCM_FORMAT_U16;
                                break;
                            case SND_PCM_FORMAT_U32:
                                alsafmt = SND_PCM_FORMAT_U16;
                                break;
                            default:
                                break;
                        };
                    }
                }

                /* set params */
                snd_pcm_hw_params_set_format(alsa_pcm,alsa_pcm_hw_params,alsafmt);
            }

            if (fmt.sample_rate != 0) {
                alsa_rate_dir = 0;
                alsa_rate = fmt.sample_rate;
                snd_pcm_hw_params_set_rate_near(alsa_pcm,alsa_pcm_hw_params,&alsa_rate,&alsa_rate_dir);
            }

            if (fmt.channels != 0) {
                alsa_channels = fmt.channels;
                snd_pcm_hw_params_set_channels(alsa_pcm,alsa_pcm_hw_params,alsa_channels);
            }

            /* apply */
	        snd_pcm_hw_params(alsa_pcm,alsa_pcm_hw_params);
            snd_pcm_hw_params_current(alsa_pcm,alsa_pcm_hw_params);
	
            /* read back */
	        if (snd_pcm_hw_params_get_channels(alsa_pcm_hw_params,&alsa_channels) < 0 ||
                snd_pcm_hw_params_get_rate(alsa_pcm_hw_params,&alsa_rate,&alsa_rate_dir) < 0 ||
                snd_pcm_hw_params_get_format(alsa_pcm_hw_params,&alsafmt) < 0) {
                return false;
            }

            if (alsa_channels > 255u) /* uint8_t limit */
                return false;

            fmt.sample_rate = alsa_rate;
            fmt.channels = (uint8_t)alsa_channels;

            switch (alsafmt) {
                case SND_PCM_FORMAT_U8:
                    fmt.bits_per_sample = 8;
                    fmt.format_tag = AFMT_PCMU;
                    break;
                case SND_PCM_FORMAT_U16:
                    fmt.bits_per_sample = 16;
                    fmt.format_tag = AFMT_PCMU;
                    break;
                case SND_PCM_FORMAT_U24:
                    fmt.bits_per_sample = 24;
                    fmt.format_tag = AFMT_PCMU;
                    break;
                case SND_PCM_FORMAT_U32:
                    fmt.bits_per_sample = 32;
                    fmt.format_tag = AFMT_PCMU;
                    break;
                case SND_PCM_FORMAT_S8:
                    fmt.bits_per_sample = 8;
                    fmt.format_tag = AFMT_PCMS;
                    break;
                case SND_PCM_FORMAT_S16:
                    fmt.bits_per_sample = 16;
                    fmt.format_tag = AFMT_PCMS;
                    break;
                case SND_PCM_FORMAT_S24:
                    fmt.bits_per_sample = 24;
                    fmt.format_tag = AFMT_PCMS;
                    break;
                case SND_PCM_FORMAT_S32:
                    fmt.bits_per_sample = 32;
                    fmt.format_tag = AFMT_PCMS;
                    break;
                default:
                    return false;
            };

            return true;
        }

        return false;
    }
    void alsa_force_close(void) {
        Close();
        alsa_close();
    }
    bool alsa_open(void) { // does NOT start capture
        int err;

        if (alsa_pcm == NULL) {
            assert(alsa_pcm_hw_params == NULL);
            if ((err=snd_pcm_open(&alsa_pcm,alsa_device_string.c_str(),SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK | SND_PCM_NO_AUTO_CHANNELS | SND_PCM_NO_AUTO_RESAMPLE/* | SND_PCM_NO_AUTO_FORMAT*/ | SND_PCM_NO_SOFTVOL)) < 0) {
                alsa_close();
                return -1;
            }
            if ((err=snd_pcm_hw_params_malloc(&alsa_pcm_hw_params)) < 0) {
                alsa_close();
                return -1;
            }
            if ((err=snd_pcm_hw_params_any(alsa_pcm,alsa_pcm_hw_params)) < 0) {
                alsa_close();
                return -1;
            }
            if ((err=snd_pcm_hw_params_set_access(alsa_pcm,alsa_pcm_hw_params,SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
                alsa_close();
                return -1;
            }
        }

        return true;
    }
    void alsa_close(void) {
        if (alsa_pcm_hw_params != NULL) {
            snd_pcm_hw_params_free(alsa_pcm_hw_params);
            alsa_pcm_hw_params = NULL;
        }
        if (alsa_pcm != NULL) {
            snd_pcm_close(alsa_pcm);
            alsa_pcm = NULL;
        }
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

    {
        AudioSourceALSA alsa;
        AudioFormat fmt;

        if (alsa.GetFormat(fmt) < 0) {
            fprintf(stderr,"Failed to get format\n");
            return 1;
        }
        fmt.bits_per_sample = 16;
        if (alsa.QueryFormat(fmt) < 0)
            fprintf(stderr,"Cannot query format\n");
        if (alsa.SetFormat(fmt) < 0)
            fprintf(stderr,"Cannot set format\n");
        if (alsa.Open() < 0)
            fprintf(stderr,"Cannot open\n");

        fprintf(stderr,"Format: type=%u rate=%lu channels=%u bitspersample=%u\n",
                fmt.format_tag,
                (unsigned long)fmt.sample_rate,
                fmt.channels,
                fmt.bits_per_sample);
    }

#if defined(HAVE_ALSA)
    snd_config_update_free_global();
#endif

    return 0;
}

