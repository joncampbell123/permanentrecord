
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
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
#include "dbfs.h"
#include "autocut.h"

#include "as_dsnd.h"

#if defined(HAVE_DSOUND)
static bool dsound_atexit_set = false;

void dsound_atexit(void) {
    snd_config_update_free_global();
}

void dsound_atexit_init(void) {
    if (!dsound_atexit_set) {
        dsound_atexit_set = 1;
        atexit(dsound_atexit);
    }
}

class AudioSourceDSOUND : public AudioSource {
public:
    AudioSourceDSOUND() : dsound_pcm(NULL), dsound_pcm_hw_params(NULL), dsound_device_string("default"), bytes_per_frame(0), samples_per_frame(0), isUserOpen(false) {
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourceDSOUND() { dsound_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            std::string sel = (str != NULL && *str != 0) ? str : "default";

            if (dsound_device_string != sel) {
                dsound_close();
                dsound_device_string = sel;
            }

            return 0;
        }

        return -EBUSY;
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
        void **hints,**n;

        dsound_atexit_init();

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
                        /* DSOUND will return NULL to mean it can do input and output */
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
    virtual bool IsOpen(void) { return (dsound_pcm != NULL) && isUserOpen; }
    virtual const char *GetSourceName(void) { return "DSOUND"; }
    virtual const char *GetDeviceName(void) { return dsound_device_string.c_str(); }

    virtual int Open(void) {
        if (!IsOpen()) {
            if (!dsound_open())
                return false;

            if (!dsound_apply_format(chosen_format)) {
                dsound_close();
                return false;
            }
            if (snd_pcm_hw_params(dsound_pcm,dsound_pcm_hw_params) < 0) {
                dsound_close();
                return false;
            }
            if (snd_pcm_prepare(dsound_pcm) < 0) {
                dsound_close();
                return false;
            }
            if (snd_pcm_start(dsound_pcm) < 0) {
                dsound_close();
                return false;
            }

            isUserOpen = true;
        }

        return true;
    }
    virtual int Close(void) {
        isUserOpen = false;
        dsound_close();
        return 0;
    }

    virtual int SetFormat(const struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        if (!dsound_open())
            return -ENODEV;

        AudioFormat tmp = fmt;
        if (!dsound_apply_format(/*&*/tmp)) {
            dsound_close();
            return -EINVAL;
        }

        chosen_format = tmp;
        chosen_format.updateFrameInfo();
        dsound_close();
        return 0;
    }
    virtual int GetFormat(struct AudioFormat &fmt) {
        if (chosen_format.format_tag == 0)
            return -EINVAL;

        fmt = chosen_format;
        return 0;
    }
    virtual int QueryFormat(struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        if (!dsound_open())
            return -ENODEV;

        if (!dsound_apply_format(/*&*/fmt)) {
            dsound_close();
            return -EINVAL;
        }

        fmt.updateFrameInfo();
        dsound_close();
        return 0;
    }
    virtual int GetAvailable(void) {
        if (IsOpen()) {
            snd_pcm_sframes_t avail=0,delay=0;

            (void)avail;
            (void)delay;

            snd_pcm_avail_delay(dsound_pcm,&avail,&delay);

            return (int)((unsigned long)avail * (unsigned long)chosen_format.bytes_per_frame);
        }

        return 0;
    }
    virtual int Read(void *buffer,unsigned int bytes) {
        if (IsOpen()) {
            unsigned int samples = bytes / chosen_format.bytes_per_frame;
            snd_pcm_sframes_t r = 0;
            int err;

            if (samples > 0) {
                r = snd_pcm_readi(dsound_pcm,buffer,samples);
                if (r >= 0) {
                    return (int)((unsigned int)r * chosen_format.bytes_per_frame);
                }
                else if (r < 0) {
                    if (r == -EPIPE) {
                        fprintf(stderr,"DSOUND warning: PCM underrun\n");
                        if ((err=snd_pcm_prepare(dsound_pcm)) < 0)
                            fprintf(stderr,"DSOUND warning: Failure to re-prepare the device after underrun, %s\n",snd_strerror(err));
                    }
                    else if (r == -EAGAIN) {
                        return 0;
                    }
                    else {
                        return (int)r;
                    }
                }
            }

            return 0;
        }

        return -EINVAL;
    }
private:
    snd_pcm_t*			        dsound_pcm;
    snd_pcm_hw_params_t*		dsound_pcm_hw_params;
    std::string                 dsound_device_string;
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
    bool dsound_apply_format(AudioFormat &fmt) {
        if (dsound_pcm != NULL && dsound_pcm_hw_params != NULL) {
            snd_pcm_format_t dsoundfmt;
            unsigned int dsound_channels = 0;
            unsigned int dsound_rate = 0;
            int	dsound_rate_dir = 0;

            if (fmt.format_tag == AFMT_PCMU) {
                if (fmt.bits_per_sample == 8)
                    dsoundfmt = SND_PCM_FORMAT_U8;
                else if (fmt.bits_per_sample == 16)
                    dsoundfmt = SND_PCM_FORMAT_U16;
                else if (fmt.bits_per_sample == 24)
                    dsoundfmt = SND_PCM_FORMAT_U24;
                else if (fmt.bits_per_sample == 32)
                    dsoundfmt = SND_PCM_FORMAT_U32;
                else
                    dsoundfmt = SND_PCM_FORMAT_U16;
            }
            else if (fmt.format_tag == AFMT_PCMS) {
                if (fmt.bits_per_sample == 8)
                    dsoundfmt = SND_PCM_FORMAT_S8;
                else if (fmt.bits_per_sample == 16)
                    dsoundfmt = SND_PCM_FORMAT_S16;
                else if (fmt.bits_per_sample == 24)
                    dsoundfmt = SND_PCM_FORMAT_S24;
                else if (fmt.bits_per_sample == 32)
                    dsoundfmt = SND_PCM_FORMAT_S32;
                else
                    dsoundfmt = SND_PCM_FORMAT_S16;
            }
            else {
                dsoundfmt = SND_PCM_FORMAT_S16;
            }

            if (snd_pcm_hw_params_test_format(dsound_pcm,dsound_pcm_hw_params,dsoundfmt) < 0) {
                switch (dsoundfmt) {
                    case SND_PCM_FORMAT_U8:
                        dsoundfmt = SND_PCM_FORMAT_S8;
                        break;
                    case SND_PCM_FORMAT_U16:
                        dsoundfmt = SND_PCM_FORMAT_S16;
                        break;
                    case SND_PCM_FORMAT_U24:
                        dsoundfmt = SND_PCM_FORMAT_S24;
                        break;
                    case SND_PCM_FORMAT_U32:
                        dsoundfmt = SND_PCM_FORMAT_S32;
                        break;
                    default:
                        break;
                };

                if (snd_pcm_hw_params_test_format(dsound_pcm,dsound_pcm_hw_params,dsoundfmt) < 0) {
                    switch (dsoundfmt) {
                        case SND_PCM_FORMAT_S8:
                            dsoundfmt = SND_PCM_FORMAT_S16;
                            break;
                        case SND_PCM_FORMAT_S16:
                            dsoundfmt = SND_PCM_FORMAT_S8;
                            break;
                        case SND_PCM_FORMAT_S24:
                            dsoundfmt = SND_PCM_FORMAT_S16;
                            break;
                        case SND_PCM_FORMAT_S32:
                            dsoundfmt = SND_PCM_FORMAT_S16;
                            break;
                        case SND_PCM_FORMAT_U8:
                            dsoundfmt = SND_PCM_FORMAT_U16;
                            break;
                        case SND_PCM_FORMAT_U16:
                            dsoundfmt = SND_PCM_FORMAT_U8;
                            break;
                        case SND_PCM_FORMAT_U24:
                            dsoundfmt = SND_PCM_FORMAT_U16;
                            break;
                        case SND_PCM_FORMAT_U32:
                            dsoundfmt = SND_PCM_FORMAT_U16;
                            break;
                        default:
                            break;
                    };
                }
            }

            /* set params */
            snd_pcm_hw_params_set_format(dsound_pcm,dsound_pcm_hw_params,dsoundfmt);

            if (fmt.sample_rate != 0) {
                dsound_rate_dir = 0;
                dsound_rate = fmt.sample_rate;
                snd_pcm_hw_params_set_rate_near(dsound_pcm,dsound_pcm_hw_params,&dsound_rate,&dsound_rate_dir);
            }

            if (fmt.channels != 0)
                dsound_channels = fmt.channels;
            else
                dsound_channels = 2;
            snd_pcm_hw_params_set_channels(dsound_pcm,dsound_pcm_hw_params,dsound_channels);
	
            /* read back */
	        if (snd_pcm_hw_params_get_channels(dsound_pcm_hw_params,&dsound_channels) < 0 ||
                snd_pcm_hw_params_get_rate(dsound_pcm_hw_params,&dsound_rate,&dsound_rate_dir) < 0 ||
                snd_pcm_hw_params_get_format(dsound_pcm_hw_params,&dsoundfmt) < 0) {
                return false;
            }

            if (dsound_channels > 255u) /* uint8_t limit */
                return false;

            fmt.sample_rate = dsound_rate;
            fmt.channels = (uint8_t)dsound_channels;

            switch (dsoundfmt) {
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
    void dsound_force_close(void) {
        Close();
        dsound_close();
    }
    bool dsound_open(void) { // does NOT start capture
        int err;

        dsound_atexit_init();

        if (dsound_pcm == NULL) {
            assert(dsound_pcm_hw_params == NULL);
            /* NTS: Prefer format conversion, or else on my laptop all audio will be 32-bit/sample recordings! */
            if ((err=snd_pcm_open(&dsound_pcm,dsound_device_string.c_str(),SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK/* | SND_PCM_NO_AUTO_CHANNELS*/ | SND_PCM_NO_AUTO_RESAMPLE/* | SND_PCM_NO_AUTO_FORMAT*/ | SND_PCM_NO_SOFTVOL)) < 0) {
                dsound_close();
                return -1;
            }
            if ((err=snd_pcm_hw_params_malloc(&dsound_pcm_hw_params)) < 0) {
                dsound_close();
                return -1;
            }
            if ((err=snd_pcm_hw_params_any(dsound_pcm,dsound_pcm_hw_params)) < 0) {
                dsound_close();
                return -1;
            }
            if ((err=snd_pcm_hw_params_set_access(dsound_pcm,dsound_pcm_hw_params,SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
                dsound_close();
                return -1;
            }
        }

        return true;
    }
    void dsound_close(void) {
        if (dsound_pcm_hw_params != NULL) {
            snd_pcm_hw_params_free(dsound_pcm_hw_params);
            dsound_pcm_hw_params = NULL;
        }
        if (dsound_pcm != NULL) {
            snd_pcm_close(dsound_pcm);
            dsound_pcm = NULL;
        }
    }
};

AudioSource* AudioSourceDSOUND_Alloc(void) {
    return new AudioSourceDSOUND();
}
#endif

