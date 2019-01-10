
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

static const GUID wasapi_CLSID_MMDeviceEnumerator =    {0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E};
static const GUID wasapi_IID_IMMDeviceEnumerator =     {0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6};
static const GUID wasapi_IID_IAudioClient =            {0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2};
static const GUID wasapi_IID_IAudioCaptureClient =     {0xc8adbd64, 0xe71e, 0x48a0, 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17};

#define wasapi_DEFINE_PROPERTYKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) \
    static const PROPERTYKEY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }

wasapi_DEFINE_PROPERTYKEY(wasapi_PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14); // DEVPROP_TYPE_STRING

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
    AudioSourceWASAPI() : bytes_per_frame(0), isUserOpen(false), readpos(0), buffer_size(0), immdevenum(NULL) {
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

        if (!wasapi_open())
            return -ENODEV;

        assert(immdevenum != NULL);
        IMMDeviceCollection *immcol = NULL;

        if (immdevenum->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE|DEVICE_STATE_UNPLUGGED,&immcol) == S_OK) {
            UINT devcount = 0,devi;

            immcol->GetCount(&devcount);
            for (devi=0;devi < devcount;devi++) {
                IMMDevice *immdev = NULL;

                if (immcol->Item(devi,&immdev) == S_OK) {
                    DWORD state = DEVICE_STATE_DISABLED;
                    LPWSTR wdid = NULL;

                    immdev->GetState(&state);
                    immdev->GetId(&wdid);

                    AudioDevicePair p;

                    if (wdid != NULL) {
                        size_t wl = wcslen(wdid);
                        OLEToCharConvertInPlace((char*)wdid,(int)wl+1/*NULL too*/);
                        p.name = (char*)wdid;
                        __CoTaskMemFree(wdid);
                    }

                    if (__PropVariantClear != NULL) {
                        IPropertyStore *props = NULL;
                        if (immdev->OpenPropertyStore(STGM_READ,&props) == S_OK) {
                            PROPVARIANT pv;

                            memset(&pv,0,sizeof(pv));
                            if (props->GetValue(wasapi_PKEY_Device_FriendlyName,&pv) == S_OK) {
                                // TODO
                                __PropVariantClear(&pv);
                            }

                            props->Release();
                        }
                    }

                    if (state == DEVICE_STATE_UNPLUGGED)
                        p.desc += " (unplugged)";

                    if (!p.name.empty())
                        names.push_back(p);

                    immcol->Release();
                }
            }

            immcol->Release();
        }

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

        if (immdevenum == NULL) {
            assert(__CoCreateInstance != NULL);

            if (__CoCreateInstance(
                wasapi_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                wasapi_IID_IMMDeviceEnumerator, (void**)(&immdevenum)) != S_OK) {
                return false;
            }
            assert(immdevenum != NULL);
        }

        return true;
    }
    void wasapi_close(void) {
        if (immdevenum != NULL) {
            immdevenum->Release();
            immdevenum = NULL;
        }
        buffer_size = 0;
        readpos = 0;
    }
private:
    DWORD                               readpos;
    DWORD                               buffer_size;
    IMMDeviceEnumerator*                immdevenum;
};

AudioSource* AudioSourceWASAPI_Alloc(void) {
    return new AudioSourceWASAPI();
}
#endif

