
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
#include "wavstruc.h"
#include "ole32.h"

#include "as_wasapi.h"

#if defined(HAVE_MMDEVICEAPI_H)
static bool wasapi_atexit_set = false;

void wasapi_atexit(void) {
    // TODO: Needed?
}

void wasapi_atexit_init(void) {
    if (!wasapi_atexit_set) {
        wasapi_atexit_set = 1;
        atexit(wasapi_atexit);
    }
}

class AudioSourceWASAPI : public AudioSource {
public:
    AudioSourceWASAPI() : bytes_per_frame(0), isUserOpen(false), readpos(0), buffer_size(0) {
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourceWASAPI() { wasapi_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            std::string sel = (str != NULL && *str != 0) ? str : "";

            if (wasapi_device_string != sel) {
                wasapi_close();
                wasapi_device_string = sel;
            }

            return 0;
        }

        return -EBUSY;
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
        if (!ole32_coinit())
            return -EINVAL;

        names.clear();

        return 0;
    }
    virtual bool IsOpen(void) { return isUserOpen; }
    virtual const char *GetSourceName(void) { return "Windows Session API (WASAPI)"; }
    virtual const char *GetDeviceName(void) { return wasapi_device_string.c_str(); }

    virtual int Open(void) {
        if (!IsOpen()) {
            if (!wasapi_open())
                return false;

            if (!wasapi_apply_format(chosen_format)) {
                wasapi_close();
                return false;
            }

            isUserOpen = true;
        }

        return true;
    }
    virtual int Close(void) {
        isUserOpen = false;
        wasapi_close();
        return 0;
    }

    virtual int SetFormat(const struct AudioFormat &fmt) {
        if (IsOpen())
            return -EBUSY;

        if (!format_is_valid(fmt))
            return -EINVAL;

        if (!wasapi_open())
            return -ENODEV;

        AudioFormat tmp = fmt;
        if (!wasapi_apply_format(/*&*/tmp)) {
            wasapi_close();
            return -EINVAL;
        }

        chosen_format = tmp;
        chosen_format.updateFrameInfo();
        wasapi_close();
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

        if (!wasapi_open())
            return -ENODEV;

        if (!wasapi_apply_format(/*&*/fmt)) {
            wasapi_close();
            return -EINVAL;
        }

        fmt.updateFrameInfo();
        wasapi_close();
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
            return 0;
        }

        return -EINVAL;
    }
private:
    std::string                 wasapi_device_string;
    AudioFormat                 chosen_format;
    unsigned int                bytes_per_frame;
    bool                        isUserOpen;
private:
    bool format_is_valid(const AudioFormat &fmt) {
        if (fmt.format_tag == AFMT_PCMU || fmt.format_tag == AFMT_PCMS) {
            if (fmt.sample_rate == 0)
                return false;
            if (fmt.channels == 0)
                return false;
            if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 || fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32))
                return false;

            return true;
        }

        return false;
    }
    bool wasapi_apply_format(AudioFormat &fmt) {
        if (fmt.format_tag == 0)
            return false;

        return false;
    }
    void wasapi_force_close(void) {
        Close();
        wasapi_close();
    }
    bool wasapi_open(void) { // does NOT start capture
        if (!ole32_coinit())
            return false;

        return false;
    }
    void wasapi_close(void) {
        buffer_size = 0;
        readpos = 0;
    }
private:
    DWORD                               readpos;
    DWORD                               buffer_size;
};

AudioSource* AudioSourceWASAPI_Alloc(void) {
    return new AudioSourceWASAPI();
}
#endif

