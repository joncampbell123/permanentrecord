
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

#include "as_pulse.h"

#if defined(HAVE_PULSE)
# include <endian.h>

static bool pulse_atexit_set = false;

static pa_context*                  pulse_context = NULL;
static pa_mainloop*                 pulse_mainloop = NULL;
static pa_mainloop_api*             pulse_mainloop_api = NULL;
static bool                         pulse_context_connected = false;

void pulse_idle(void) {
    if (pulse_mainloop == NULL || pulse_mainloop_api == NULL)
        return;

    pa_mainloop_prepare(pulse_mainloop,100);
    pa_mainloop_poll(pulse_mainloop);
    pa_mainloop_dispatch(pulse_mainloop);
}

bool pulse_open_global_connect(void) {
    if (pulse_context == NULL)
        return false;

    if (!pulse_context_connected) {
        if (pa_context_connect(pulse_context, NULL, (pa_context_flags_t)0, NULL) < 0)
            return false;

        /* wait for connection. */
        {
            pa_context_state_t st;

            do {
                pulse_idle();

                st = pa_context_get_state(pulse_context);
                if (st == PA_CONTEXT_CONNECTING ||
                    st == PA_CONTEXT_AUTHORIZING ||
                    st == PA_CONTEXT_SETTING_NAME) {
                    usleep(1000);
                }
                else if (st == PA_CONTEXT_READY) {
                    break;
                }
                else {
                    return false;
                }
            } while(1);
        }

        pulse_context_connected = true;
    }

    return true;
}

bool pulse_open_global(void) {
    if (pulse_mainloop == NULL) {
        if ((pulse_mainloop=pa_mainloop_new()) == NULL)
            return false;
    }
    if (pulse_mainloop_api == NULL) {
        if ((pulse_mainloop_api=pa_mainloop_get_api(pulse_mainloop)) == NULL)
            return false;
    }
    if (pulse_context == NULL) {
        if ((pulse_context=pa_context_new(pulse_mainloop_api,NULL)) == NULL)
            return false;
    }

    if (!pulse_open_global_connect())
        return false;

    return true;
}

void pulse_close_global(void) {
    pulse_context_connected = false;
    if (pulse_context != NULL) {
        pa_context_disconnect(pulse_context);
        pa_context_unref(pulse_context);
        pulse_context = NULL;
    }
    if (pulse_mainloop != NULL) {
        pa_signal_done();
        pa_mainloop_free(pulse_mainloop);
        pulse_mainloop = NULL;
    }
    pulse_mainloop_api = NULL;
}

void pulse_atexit(void) {
    pulse_close_global();
}

void pulse_atexit_init(void) {
    if (!pulse_atexit_set) {
        pulse_atexit_set = 1;
        atexit(pulse_atexit);
    }
}

class AudioSourcePULSE : public AudioSource {
public:
    AudioSourcePULSE() : pulse_stream(NULL), bytes_per_frame(0), samples_per_frame(0), isUserOpen(false), pending_data(NULL), pending_data_read(NULL), pending_data_fence(NULL) {
        pasampspec.format = PA_SAMPLE_INVALID;
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourcePULSE() { pulse_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            std::string sel = (str != NULL && *str != 0) ? str : "";

            if (pulse_device_string != sel) {
                pulse_close();
                pulse_device_string = sel;
            }

            return 0;
        }

        return -EBUSY;
    }
    static void cb_source(pa_context *ctx,const pa_source_info *info,int eol,void *userdata) {
        if (eol) return;
        assert(userdata != NULL);
        assert(info != NULL);
        (void)ctx;
        (void)eol;

        std::vector<AudioDevicePair> &names = *((std::vector<AudioDevicePair>*)userdata);

        AudioDevicePair ent;
        if (info->name != NULL) ent.name = info->name;
        if (info->description != NULL) ent.desc = info->description;
        names.push_back(ent);
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
        pulse_atexit_init();

        names.clear();
        if (!pulse_open_global())
            return -ENODEV;

        assert(pulse_context != NULL);

        pa_operation *pa = pa_context_get_source_info_list(pulse_context,cb_source,(void*)(&names));
        if (pa == NULL)
            return -ENOMEM;

        pa_operation_state past;

        while ((past=pa_operation_get_state(pa)) == PA_OPERATION_RUNNING)
            pulse_idle();

        if (past != PA_OPERATION_DONE)
            return -ENODEV;

        return 0;
    }
    virtual bool IsOpen(void) { return (pulse_stream != NULL) && isUserOpen; }
    virtual const char *GetSourceName(void) { return "PULSE"; }
    virtual const char *GetDeviceName(void) { return pulse_device_string.c_str(); }

    virtual int Open(void) {
        if (!IsOpen()) {
            if (!pulse_open())
                return -1;

            isUserOpen = true;
        }

        return 0;
    }
    virtual int Close(void) {
        isUserOpen = false;
        pulse_close();
        return 0;
    }

    virtual int SetFormat(const struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        chosen_format = fmt;
        if (!pulse_open()) {
            pulse_close();
            chosen_format.format_tag = 0;
            return -ENODEV;
        }

        chosen_format = fmt;
        chosen_format.updateFrameInfo();
        pulse_close();
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

        if (!pulse_open()) {
            pulse_close();
            return -ENODEV;
        }

        fmt.updateFrameInfo();
        pulse_close();
        return 0;
    }
    virtual int GetAvailable(void) {
        if (IsOpen()) {
            return 0;
        }

        return 0;
    }
    virtual int Read(void *buffer,unsigned int bytes) {
        if (IsOpen()) {
            unsigned char *d = (unsigned char*)buffer;
            int rd = 0;

            while (bytes > 0) {
                assert(pulse_stream != NULL);
                pulse_idle();

                if (pending_data == NULL) {
                    const void *ptr = NULL;
                    size_t len = 0;

                    if (pa_stream_peek(pulse_stream,&ptr,&len) < 0)
                        return -EIO;
                    if (ptr == NULL && len == 0)
                        break;

                    /* NTS: ptr == NULL and len > 0 if a "hole" */
                    if (len > 0) {
                        if (!pending_data_alloc(len))
                            return -ENOMEM;

                        memcpy(pending_data,ptr,len);
                    }

                    pa_stream_drop(pulse_stream);
                }

                if (pending_data != NULL && pending_data_read < pending_data_fence) {
                    unsigned int proc = (unsigned int)(pending_data_fence - pending_data_read);
                    if (proc > bytes) proc = bytes;
                    assert(proc != 0);

                    memcpy(d,pending_data_read,proc);
                    pending_data_read += proc;
                    rd += (int)proc;
                    bytes -= proc;
                    d += proc;

                    assert(pending_data_read <= pending_data_fence);
                    if (pending_data_read >= pending_data_fence)
                        pending_data_free();
                }
            }

            return rd;
        }

        return -EINVAL;
    }
private:
    /* arrrrgh PulseAudio why doesn't your API just allow me to ask for some number of samples? */
    void pending_data_free(void) {
        if (pending_data != NULL) {
            delete[] pending_data;
            pending_data = NULL;
        }
        pending_data_read = NULL;
        pending_data_fence = NULL;
    }
    bool pending_data_alloc(const size_t len) { /* this ASSUMES you've already consumed the existing data! */
        pending_data_free();
        if (len != 0) {
            if (len > (8*1024*1024)) return false;
            pending_data = new(std::nothrow) unsigned char[len];
            if (pending_data == NULL) return false;
            pending_data_read = pending_data;
            pending_data_fence = pending_data + len;
            return true;
        }

        return true;
    }
private:
    pa_sample_spec              pasampspec;
    pa_stream*                  pulse_stream;
    std::string                 pulse_device_string;
    AudioFormat                 chosen_format;
    unsigned int                bytes_per_frame;
    unsigned int                samples_per_frame;
    bool                        isUserOpen;
    unsigned char*              pending_data;
    unsigned char*              pending_data_read;
    unsigned char*              pending_data_fence;
private:
    bool format_is_valid(const AudioFormat &fmt) {
        if (fmt.sample_rate == 0)
            return false;
        if (fmt.channels == 0)
            return false;
        if (fmt.bits_per_sample == 0)
            return false;
        if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 || fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32))
            return false;
        if (fmt.format_tag == AFMT_PCMU) {
            if (fmt.bits_per_sample != 8)
                return false;
        }
        else if (fmt.format_tag == AFMT_PCMS) {
            if (fmt.bits_per_sample == 8)
                return false;
        }
        else {
            return false;
        }

        return true;
    }
    void pulse_force_close(void) {
        Close();
        pulse_close();
    }
    bool pulse_open(void) { // does NOT start capture
        pulse_atexit_init();

        if (chosen_format.format_tag == 0)
            return false;

        if (chosen_format.format_tag == AFMT_PCMU && chosen_format.bits_per_sample == 8) {
            pasampspec.format = PA_SAMPLE_U8;
        }
        else if (chosen_format.format_tag == AFMT_PCMS) {
#if BYTE_ORDER == BIG_ENDIAN
            if (chosen_format.bits_per_sample == 16)
                pasampspec.format = PA_SAMPLE_S16BE;
            else if (chosen_format.bits_per_sample == 24)
                pasampspec.format = PA_SAMPLE_S24BE;
            else if (chosen_format.bits_per_sample == 32)
                pasampspec.format = PA_SAMPLE_S32BE;
#else
            if (chosen_format.bits_per_sample == 16)
                pasampspec.format = PA_SAMPLE_S16LE;
            else if (chosen_format.bits_per_sample == 24)
                pasampspec.format = PA_SAMPLE_S24LE;
            else if (chosen_format.bits_per_sample == 32)
                pasampspec.format = PA_SAMPLE_S32LE;
#endif
            else
                return false;
        }
        else {
            return false;
        }

        pasampspec.rate = chosen_format.sample_rate;
        pasampspec.channels = chosen_format.channels;

        if (!pulse_open_global())
            return false;

        assert(pulse_stream == NULL);
        assert(pulse_context != NULL);
        assert(pulse_mainloop != NULL);
        assert(pulse_mainloop_api != NULL);

        if ((pulse_stream=pa_stream_new(pulse_context,"permanentrecord",&pasampspec,NULL)) == NULL) {
            pulse_close();
            return false;
        }
        if (pa_stream_connect_record(pulse_stream,pulse_device_string.empty() ? NULL : pulse_device_string.c_str(),NULL,(pa_stream_flags_t)0) < 0) {
            pulse_close();
            return false;
        }
        pulse_idle();

        {
            pa_stream_state_t st;

            do {
                pulse_idle();
                st = pa_stream_get_state(pulse_stream);
                if (st == PA_STREAM_CREATING) {
                    usleep(10000);
                }
                else if (st == PA_STREAM_READY) {
                    break;
                }
                else {
                    pulse_close();
                    return false;
                }
            } while (1);
        }

        return true;
    }
    void pulse_close(void) {
        pending_data_free();
        if (pulse_stream != NULL) {
            pa_stream_flush(pulse_stream,NULL,NULL);
            pa_stream_unref(pulse_stream);
            pulse_stream = NULL;
        }
        pasampspec.format = PA_SAMPLE_INVALID;
    }
};

AudioSource* AudioSourcePULSE_Alloc(void) {
    return new AudioSourcePULSE();
}
#endif

