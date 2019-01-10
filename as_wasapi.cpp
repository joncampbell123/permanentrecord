
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
static const GUID wasapi_IID_IMMEndpoint =             {0x1be09788, 0x6894, 0x4089, 0x85, 0x86, 0x9a, 0x2a, 0x6c, 0x26, 0x5a, 0xc5};

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
    AudioSourceWASAPI() : bytes_per_frame(0), isUserOpen(false), readpos(0), buffer_size(0), immdevenum(NULL), immdev(NULL), immacl(NULL), immacapcl(NULL) {
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
    void immcol_enum(std::vector<AudioDevicePair> &names,IMMDeviceCollection *immcol) {
        UINT devcount = 0,devi;

        immcol->GetCount(&devcount);
        for (devi=0;devi < devcount;devi++) {
            IMMDevice *eimmdev = NULL;

            if (immcol->Item(devi,&eimmdev) == S_OK) {
                DWORD state = DEVICE_STATE_DISABLED;
                LPWSTR wdid = NULL;

                eimmdev->GetState(&state);
                eimmdev->GetId(&wdid);

                AudioDevicePair p;

                if (wdid != NULL) {
                    size_t wl = wcslen(wdid);
                    OLEToCharConvertInPlace((char*)wdid,(int)wl+1/*NULL too*/);
                    p.name = (char*)wdid;
                    __CoTaskMemFree(wdid);
                }

                if (__PropVariantClear != NULL) {
                    IPropertyStore *props = NULL;
                    if (eimmdev->OpenPropertyStore(STGM_READ,&props) == S_OK) {
                        PROPVARIANT pv;

                        // FIXME: Ick, figure out all the prop variant crap.
                        //        Until then, this works too.
                        memset(&pv,0,sizeof(pv));
                        if (props->GetValue(wasapi_PKEY_Device_FriendlyName,&pv) == S_OK) {
                            wchar_t tmp[256];
                            UINT tmp_sz = (sizeof(tmp) / sizeof(tmp[0])) - 1;

                            tmp[0] = 0;
                            if ((pv.vt & VT_TYPEMASK) == VT_LPWSTR && pv.pwszVal != NULL) {
                                wcsncpy(tmp,pv.pwszVal,tmp_sz);
                                tmp[tmp_sz] = 0;
                            }

                            OLEToCharConvertInPlace((char*)tmp,(int)wcslen(tmp)+1);
                            p.desc = (char*)tmp;

                            __PropVariantClear(&pv);
                        }

                        props->Release();
                    }
                }

                if (state == DEVICE_STATE_UNPLUGGED)
                    p.desc += " (unplugged)";

                if (!p.name.empty())
                    names.push_back(p);
            }
        }
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
            immcol_enum(names,immcol);
            immcol->Release();
        }

        if (immdevenum->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE|DEVICE_STATE_UNPLUGGED,&immcol) == S_OK) {
            immcol_enum(names,immcol);
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
                return -1;

            if (!wasapi_apply_format(chosen_format)) {
                wasapi_close();
                return -1;
            }

            bytes_per_frame = chosen_format.bytes_per_frame;

            immacl->Reset();
            if (immacl->Start() != S_OK) {
                fprintf(stderr,"Failed to start\n");
                return -1;
            }

            if (immacl->GetService(wasapi_IID_IAudioCaptureClient,(void**)(&immacapcl)) != S_OK) {
                fprintf(stderr,"Failed to get capture client interface\n");
                return -1;
            }

            isUserOpen = true;
        }

        return 0;
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
    bool UpdateFormatFromWAVE(struct AudioFormat &fmt,WAVEFORMATEX *wfx) {
        fmt.sample_rate = wfx->nSamplesPerSec;
        fmt.channels = (uint8_t)wfx->nChannels;

        if (fmt.format_tag == 0) {
            fmt.format_tag = AFMT_PCMS;
            fmt.bits_per_sample = 16;
        }

        fmt.updateFrameInfo();
 
        if (wfx->wFormatTag == 0x0001/*WAVE_FORMAT_PCM*/) {
            if (wfx->wBitsPerSample < 8 || wfx->wBitsPerSample > 32)
                return false;
            if (wfx->nChannels < 1 || wfx->nChannels > 8)
                return false;
            if (wfx->nSamplesPerSec < 1000 || wfx->nSamplesPerSec > 192000)
                return false;

            if (wfx->wBitsPerSample == 8)
                fmt.format_tag = AFMT_PCMU;
            else
                fmt.format_tag = AFMT_PCMS;

            fmt.bits_per_sample = (uint8_t)wfx->wBitsPerSample;
            fmt.sample_rate = wfx->nSamplesPerSec;
            fmt.channels = (uint8_t)wfx->nChannels;

            fmt.updateFrameInfo();
        }
        else if (wfx->wFormatTag == 0xFFFE/*WAVE_FORMAT_EXTENSIBLE*/) {
            WAVEFORMATEXTENSIBLE *wext = (WAVEFORMATEXTENSIBLE*)wfx;

            if (!memcmp(&wext->SubFormat,&windows_KSDATAFORMAT_SUBTYPE_PCM,sizeof(GUID))) {
                if (wfx->wBitsPerSample < 8 || wfx->wBitsPerSample > 32)
                    return false;
                if (wfx->nChannels < 1 || wfx->nChannels > 8)
                    return false;
                if (wfx->nSamplesPerSec < 1000 || wfx->nSamplesPerSec > 192000)
                    return false;

                if (wfx->wBitsPerSample == 8)
                    fmt.format_tag = AFMT_PCMU;
                else
                    fmt.format_tag = AFMT_PCMS;

                fmt.bits_per_sample = (uint8_t)wfx->wBitsPerSample;
                fmt.sample_rate = wfx->nSamplesPerSec;
                fmt.channels = (uint8_t)wfx->nChannels;

                fmt.updateFrameInfo();
            }
            /* FIXME: I'm seeing the "get mix format" call return EXTENSIBLE with floating point format */
            else {
                return false;
            }
        }
        else {
            return false;
        }

        return true;
    }
    virtual int GetFormat(struct AudioFormat &fmt) {
        if (chosen_format.format_tag == 0) {
            if (!IsOpen()) {
                if (!wasapi_open())
                    return -EINVAL;

                if (immacl == NULL) {
                    wasapi_close();
                    return -EINVAL;
                }

                WAVEFORMATEX *wfx = NULL;

                if (immacl->GetMixFormat(&wfx) != S_OK) {
                    wasapi_close();
                    return -EINVAL;
                }

                UpdateFormatFromWAVE(fmt,wfx);

                __CoTaskMemFree(wfx);
            }
        }

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

        if (!wasapi_open())
            return false;
        if (immacl == NULL)
            return false;

        WAVEFORMATEXTENSIBLE wext;

        memset(&wext,0,sizeof(wext));

        if (fmt.bits_per_sample > 16 || fmt.channels > 2) {
            wext.Format.wFormatTag = 0xFFFE;/*WAVE_FORMAT_EXTENSIBLE*/
            memcpy(&wext.SubFormat,&windows_KSDATAFORMAT_SUBTYPE_PCM,sizeof(GUID));
            wext.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            wext.dwChannelMask = (1ul << fmt.channels) - 1ul;
            wext.Samples.wValidBitsPerSample = fmt.bits_per_sample;
        }
        else {
            wext.Format.wFormatTag = 0x0001;/*WAVE_FORMAT_PCM*/
        }
        wext.Format.nChannels = fmt.channels;
        wext.Format.nSamplesPerSec = fmt.sample_rate;
        wext.Format.wBitsPerSample = fmt.bits_per_sample;
        wext.Format.nBlockAlign = (WORD)(fmt.channels * ((fmt.bits_per_sample + 7u) / 8u));
        wext.Format.nAvgBytesPerSec = wext.Format.nBlockAlign * wext.Format.nSamplesPerSec;

        DWORD flags = 0;

        {
            IMMEndpoint *ep = NULL;
            if (immdev->QueryInterface(wasapi_IID_IMMEndpoint,(void**)(&ep)) == S_OK) {
                EDataFlow flow;

                if (ep->GetDataFlow(&flow) == S_OK) {
                    if (flow == eRender) {
                        fprintf(stderr,"Preparing capture from render endpoint\n");
                        flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
                    }
                }
            }
        }

        immacl->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,10000000/*100ns units = 1 second */,0,(const WAVEFORMATEX*)(&wext),NULL);

        {
            WAVEFORMATEX *wfx = NULL;

            if (immacl->GetMixFormat(&wfx) == S_OK)
                UpdateFormatFromWAVE(fmt,wfx);

            __CoTaskMemFree(wfx);
        }

        return true;
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
        if (immdev == NULL) {
            if (!wasapi_device_string.empty()) {
                wchar_t wtmp[256];
                unsigned int i;
                const char *s;

                i = 0;
                s = wasapi_device_string.c_str();
                while (*s != 0) {
                    if (i >= 255) return false;
                    wtmp[i++] = (wchar_t)(*s++);
                }
                wtmp[i] = 0;

                if (immdevenum->GetDevice(wtmp,&immdev) != S_OK)
                    return false;
            }
            else {
                if (immdevenum->GetDefaultAudioEndpoint(eCapture,eMultimedia,&immdev) != S_OK)
                    return false;
            }
        }
        if (immacl == NULL) {
            if (immdev->Activate(wasapi_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)(&immacl)) != S_OK)
                return false;
        }

        return true;
    }
    void wasapi_close(void) {
        if (immacapcl != NULL) {
            immacapcl->Release();
            immacapcl = NULL;
        }
        if (immacl != NULL) {
            immacl->Stop();
            immacl->Release();
            immacl = NULL;
        }
        if (immdev != NULL) {
            immdev->Release();
            immdev = NULL;
        }
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
    IMMDevice*                          immdev;
    IAudioClient*                       immacl;
    IAudioCaptureClient*                immacapcl;
};

AudioSource* AudioSourceWASAPI_Alloc(void) {
    return new AudioSourceWASAPI();
}
#endif

