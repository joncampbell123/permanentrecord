
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

#if defined(HAVE_DSOUND_H)
static bool dsound_atexit_set = false;
static HMODULE dsound_dll = NULL;
static HMODULE ole32_dll = NULL;

void dsound_atexit(void) {
	if (ole32_dll != NULL) {
		FreeLibrary(ole32_dll);
		ole32_dll = NULL;
	}
	if (dsound_dll != NULL) {
		FreeLibrary(dsound_dll);
		dsound_dll = NULL;
	}
}

void dsound_atexit_init(void) {
    if (!dsound_atexit_set) {
        dsound_atexit_set = 1;
        atexit(dsound_atexit);
    }
}

static HRESULT (WINAPI *__DirectSoundEnumerate)(LPDSENUMCALLBACK lpDSEnumCallback,LPVOID lpContext) = NULL;
static int (WINAPI *__StringFromGUID2)(REFGUID rguid,LPOLESTR lpsz,int cchMax) = NULL;
static HRESULT (WINAPI *__CLSIDFromString)(LPOLESTR lpsz,LPCLSID pclsid) = NULL;

void OLEToCharConvertInPlace(char *sz,int cch) {
	/* convert in place, cch chars of wchar_t to cch chars of char. cch should include the NUL character. */
	/* cch is assumed to be the valid buffer size, this code will not go past the end of the buffer. */
	/* this is used for calls that are primarily ASCII and do not need to worry about locale,
	 * yet for whatever reason Microsoft insisted on using OLECHAR (wchar_t) */
	wchar_t *sw = (wchar_t*)sz;
	int i = 0;

	while (i < cch) {
		wchar_t c = sw[i];

		if (c >= 0x80)
			sz[i] = '?';
		else
			sz[i] = (char)c;

		i++;
	}
}

// This OLE32 function deals in WCHAR, we need TCHAR
HRESULT ans_CLSIDFromString(char *sz,LPCLSID pclsid) {
	wchar_t tmp[128]; // should be large enough for GUID strings
	unsigned int i;

	i=0;
	while (i < 127 && sz[i] != 0) {
		if (sz[i] > 0x7F) return E_FAIL;
		tmp[i] = (wchar_t)sz[i];
		i++;
	}
	tmp[i] = 0;
	if (i >= 127)
		return E_FAIL;

	return __CLSIDFromString((LPOLESTR)tmp,pclsid);
}

// This OLE32 function deals in WCHAR, we need TCHAR
int ans_StringFromGUID2(REFGUID rguid,char *sz,int cchMax) {
	int r;

	r = __StringFromGUID2(rguid,(LPOLESTR)sz,/*size from chars to wchar_t of buffer*/cchMax / sizeof(wchar_t));
	/* r = chars including NULL terminator (bytes is r * sizeof(wchar_t) */
	OLEToCharConvertInPlace(sz,r);
	return r;
}

bool dsound_dll_init(void) {
	if (dsound_dll == NULL) {
		if ((dsound_dll=LoadLibrary("DSOUND.DLL")) == NULL)
			return false;

		dsound_atexit_init();

		__DirectSoundEnumerate =
			(HRESULT (WINAPI*)(LPDSENUMCALLBACK,LPVOID))
			GetProcAddress(dsound_dll,"DirectSoundEnumerateA");
		if (__DirectSoundEnumerate == NULL)
			return false;
	}
	if (ole32_dll == NULL) {
		if ((ole32_dll=LoadLibrary("OLE32.DLL")) == NULL)
			return false;

		dsound_atexit_init();

		__StringFromGUID2 =
			(int (WINAPI*)(REFGUID,LPOLESTR,int))
			GetProcAddress(ole32_dll,"StringFromGUID2");
		if (__StringFromGUID2 == NULL)
			return false;

		__CLSIDFromString =
			(HRESULT (WINAPI *)(LPOLESTR,LPCLSID))
			GetProcAddress(ole32_dll,"CLSIDFromString");
		if (__CLSIDFromString == NULL)
			return false;
	}

	return true;
}

class AudioSourceDSOUND : public AudioSource {
public:
    AudioSourceDSOUND() : bytes_per_frame(0), samples_per_frame(0), isUserOpen(false) {
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
    static BOOL CALLBACK cb_dsenum(LPGUID lpGuid,LPCSTR lpcstrDescription,LPCSTR lpcstrModule,LPVOID lpContext) {
            assert(lpContext != NULL);
            std::vector<AudioDevicePair> &names = *((std::vector<AudioDevicePair>*)lpContext);

	    AudioDevicePair p;
	    char tmp[256];

	    tmp[sizeof(tmp)-1] = 0;
	    ans_StringFromGUID2(*lpGuid,tmp,sizeof(tmp)-1);

	    p.name = tmp;
	    p.desc = lpcstrDescription != NULL ? lpcstrDescription : "";
	    if (lpcstrModule != NULL && *lpcstrModule != 0) {
		    p.desc += " ";
		    p.desc += lpcstrModule;
	    }

	    names.push_back(p);

	    return TRUE;
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
	if (!dsound_dll_init())
		return -EINVAL;

        names.clear();

	if (__DirectSoundEnumerate(cb_dsenum,(void*)(&names)) != DS_OK)
		return -EINVAL;

	return 0;
    }
    virtual bool IsOpen(void) { return isUserOpen; }
    virtual const char *GetSourceName(void) { return "DirectSound (DirectX)"; }
    virtual const char *GetDeviceName(void) { return dsound_device_string.c_str(); }

    virtual int Open(void) {
        if (!IsOpen()) {
            if (!dsound_open())
                return false;

            if (!dsound_apply_format(chosen_format)) {
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
        return 0;
    }
    virtual int Read(void *buffer,unsigned int bytes) {
        return -EINVAL;
    }
private:
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
            if (!(fmt.bits_per_sample == 8 || fmt.bits_per_sample == 16 || fmt.bits_per_sample == 24 || fmt.bits_per_sample == 32))
                return false;

            return true;
        }

        return false;
    }
    bool dsound_apply_format(AudioFormat &fmt) {
        return false;
    }
    void dsound_force_close(void) {
        Close();
        dsound_close();
    }
    bool dsound_open(void) { // does NOT start capture
        int err;

        dsound_atexit_init();

        return false;
    }
    void dsound_close(void) {
    }
};

AudioSource* AudioSourceDSOUND_Alloc(void) {
    return new AudioSourceDSOUND();
}
#endif

