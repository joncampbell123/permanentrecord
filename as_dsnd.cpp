
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

static HRESULT (WINAPI *__DirectSoundCaptureCreate)(LPCGUID lpcGUID,LPDIRECTSOUNDCAPTURE8 * lplpDSC,LPUNKNOWN pUnkOuter) = NULL;
static HRESULT (WINAPI *__DirectSoundCaptureEnumerate)(LPDSENUMCALLBACK lpDSEnumCallback,LPVOID lpContext) = NULL;
static int (WINAPI *__StringFromGUID2)(REFGUID rguid,LPOLESTR lpsz,int cchMax) = NULL;
static HRESULT (WINAPI *__CLSIDFromString)(LPOLESTR lpsz,LPCLSID pclsid) = NULL;

static void OLEToCharConvertInPlace(char *sz,int cch) {
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
static HRESULT ans_CLSIDFromString(const char *sz,LPCLSID pclsid) {
	wchar_t tmp[128]; // should be large enough for GUID strings
	unsigned int i;

	i=0;
	while (i < 127 && sz[i] != 0) {
		if ((unsigned char)sz[i] > 0x7Fu) return E_FAIL;
		tmp[i] = (wchar_t)sz[i];
		i++;
	}
	tmp[i] = 0;
	if (i >= 127)
		return E_FAIL;

	return __CLSIDFromString((LPOLESTR)tmp,pclsid);
}

// This OLE32 function deals in WCHAR, we need TCHAR
static int ans_StringFromGUID2(REFGUID rguid,char *sz,int cchMax) {
	int r;

	r = __StringFromGUID2(rguid,(LPOLESTR)sz,/*size from chars to wchar_t of buffer*/(int)((unsigned int)cchMax / sizeof(wchar_t)));
	/* r = chars including NULL terminator (bytes is r * sizeof(wchar_t) */
	OLEToCharConvertInPlace(sz,r);
	return r;
}

bool dsound_dll_init(void) {
	if (dsound_dll == NULL) {
		if ((dsound_dll=LoadLibrary("DSOUND.DLL")) == NULL)
			return false;

		dsound_atexit_init();

		__DirectSoundCaptureEnumerate =
			(HRESULT (WINAPI*)(LPDSENUMCALLBACK,LPVOID))
			GetProcAddress(dsound_dll,"DirectSoundCaptureEnumerateA");
		if (__DirectSoundCaptureEnumerate == NULL)
			return false;

		__DirectSoundCaptureCreate =
			(HRESULT (WINAPI*)(LPCGUID,LPDIRECTSOUNDCAPTURE8*,LPUNKNOWN))
			GetProcAddress(dsound_dll,"DirectSoundCaptureCreate");
		if (__DirectSoundCaptureCreate == NULL)
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
    AudioSourceDSOUND() : bytes_per_frame(0), isUserOpen(false), dsndcap(NULL), dsndcapbuf(NULL), readpos(0), buffer_size(0) {
        chosen_format.bits_per_sample = 0;
        chosen_format.sample_rate = 0;
        chosen_format.format_tag = 0;
        chosen_format.channels = 0;
    }
    virtual ~AudioSourceDSOUND() { dsound_force_close(); }
public:
    virtual int SelectDevice(const char *str) {
        if (!IsOpen()) {
            std::string sel = (str != NULL && *str != 0) ? str : "";

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

        // NTS: lpGuid == NULL for primary device
        if (lpGuid != NULL) {
            tmp[sizeof(tmp)-1] = 0;
            ans_StringFromGUID2(*lpGuid,tmp,sizeof(tmp)-1);
            p.name = tmp;
        }

        p.desc = lpcstrDescription != NULL ? lpcstrDescription : "";
        if (lpcstrModule != NULL && *lpcstrModule != 0) {
            p.desc += " [";
            p.desc += lpcstrModule;
            p.desc += "]";
        }

        names.push_back(p);

        return TRUE;
    }
    virtual int EnumDevices(std::vector<AudioDevicePair> &names) {
        if (!dsound_dll_init())
            return -EINVAL;

        names.clear();

        if (__DirectSoundCaptureEnumerate(cb_dsenum,(void*)(&names)) != DS_OK)
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

            if (dsndcap == NULL || dsndcapbuf == NULL) {
                dsound_close();
                return false;
            }

	    bytes_per_frame = chosen_format.bytes_per_frame;
            if (dsndcapbuf->Start(DSCBSTART_LOOPING) != DS_OK) {
                dsound_close();
                return false;
            }

            /* wherever it is, that is the starting point */
            {
                DWORD x;

                readpos = 0;
                if (dsndcapbuf->GetCurrentPosition(&x/*capture*/,&readpos) != DS_OK)
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
		return 0;
	}

        return 0;
    }
    virtual int Read(void *buffer,unsigned int bytes) {
	if (IsOpen() && dsndcapbuf != NULL) {
		unsigned char *dbuf = (unsigned char*)buffer;
		int patience = 2;
		int rd = 0;

		/* please keep bytes at a multiple of a frame */
		bytes -= bytes % bytes_per_frame;

		/* sanity check */
		if (readpos == buffer_size) readpos = 0;

		/* process */
		while (bytes > 0 && patience-- > 0) {
			DWORD ncap=0,nread=readpos;
			DWORD ptrlen=0;
			void *ptr=NULL;

			if (dsndcapbuf->GetCurrentPosition(&ncap,&nread) != DS_OK)
				break;

			/* sanity check */
			if (nread > buffer_size) nread = buffer_size;

			/* NTS: Experience with DirectSound under older versions of Windows (Windows 98 for example)
			 *      and ISA-type devices says that it is possible for the read position to sit at an
			 *      offset NOT a multiple of a frame. For example, back in the day on a laptop with
			 *      Windows 98 and a OPL3-SAx sound card, 16-bit stereo capture could return an offset
			 *      that points at the R sample in a frame instead of the L sample. */
			nread -= nread % bytes_per_frame;

			/* how much to process? */
			int howmuch = (int)nread - (int)readpos;
			if (howmuch < 0) howmuch += (int)buffer_size;
			if (howmuch <= 0) break;
			if ((unsigned int)howmuch > bytes) howmuch = (int)bytes;

			/* let's not deal with two buffers considering the circular buffer, keep this code simple */
			DWORD cando = buffer_size - readpos;
			if ((DWORD)howmuch > cando) howmuch = (int)cando;
			if (howmuch == 0) break;

			/* lock the buffer and go */
			if (dsndcapbuf->Lock(readpos,(DWORD)howmuch,&ptr,&ptrlen,NULL,NULL,0) != DS_OK) {
				fprintf(stderr,"Lock error readpos %u howmuch %u bufferlen %u\n",
					(unsigned int)readpos,(unsigned int)howmuch,(unsigned int)buffer_size);
				break;
			}

			/* sanity check. we didn't ask for wraparound so it shouldn't happen. */
			if (ptrlen != (DWORD)howmuch)
				fprintf(stderr,"Lock warning, less locked than requested %u < %u\n",
					(unsigned int)ptrlen,(unsigned int)howmuch);
			if (ptr == NULL)
				fprintf(stderr,"Lock warning, ptr == NULL on return (readpos %u howmuch %u bufferlen %u)\n",
					(unsigned int)readpos,(unsigned int)howmuch,(unsigned int)buffer_size);

			if (ptrlen == (DWORD)howmuch && ptr != NULL) {
				memcpy(dbuf,ptr,(size_t)howmuch);
				bytes -= (unsigned int)howmuch;
				readpos += (DWORD)howmuch;
				dbuf += howmuch;
				rd += howmuch;

				if (readpos > buffer_size)
					fprintf(stderr,"Lock warning, readpos overrun\n");
				if (readpos >= buffer_size)
					readpos = 0;
			}

			/* unlock */
			if (dsndcapbuf->Unlock(ptr,ptrlen,NULL,0) != DS_OK) {
				fprintf(stderr,"Unlock error\n");
				break;
			}
		}

		return rd;
	}

        return -EINVAL;
    }
private:
    std::string                 dsound_device_string;
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
    bool dsound_apply_format(AudioFormat &fmt) {
        if (fmt.format_tag == 0)
            return false;

        if (dsndcap == NULL)
            return false;

        if (fmt.format_tag == 0)
            return false;

        DSCBUFFERDESC dsc;
        windows_WAVEFORMATEXTENSIBLE wfmt;

        switch (fmt.format_tag) {
            case AFMT_PCMU:
            case AFMT_PCMS:
                wfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
                wfmt.Format.nChannels = fmt.channels;
                wfmt.Format.nSamplesPerSec = fmt.sample_rate;
                wfmt.Format.wBitsPerSample = fmt.bits_per_sample;
                wfmt.Format.nBlockAlign = (WORD)(((fmt.bits_per_sample + 7u) / 8u) * (unsigned int)fmt.channels);
                wfmt.Format.nAvgBytesPerSec = wfmt.Format.nBlockAlign * wfmt.Format.nSamplesPerSec;
                wfmt.Format.cbSize = 0;

                if (fmt.channels > 2 || fmt.bits_per_sample > 16) {
                    wfmt.Format.cbSize = sizeof(windows_WAVEFORMATEXTENSIBLE) - sizeof(windows_WAVEFORMATEX);
                    wfmt.Samples.wValidBitsPerSample = fmt.bits_per_sample;
                    wfmt.dwChannelMask = (1ul << (unsigned long)fmt.channels) - 1ul;
                    wfmt.SubFormat = windows_KSDATAFORMAT_SUBTYPE_PCM;
                }

		if (fmt.bits_per_sample == 8)
			fmt.format_tag = AFMT_PCMU;
		else
			fmt.format_tag = AFMT_PCMS;
                break;
            default:
                return false;
        }

        if (dsndcapbuf != NULL) {
            dsndcapbuf->Release();
            dsndcapbuf = NULL;
        }

        memset(&dsc,0,sizeof(dsc));
        dsc.dwSize = sizeof(DSBUFFERDESC1); // NTS: DirectX 7.0 or older compat. We don't care for WinXP FX
        dsc.dwBufferBytes = fmt.sample_rate * ((fmt.bits_per_sample + 7u) / 8u) * fmt.channels;
        dsc.lpwfxFormat = (WAVEFORMATEX*)(&wfmt);

	buffer_size = dsc.dwBufferBytes;

        if (dsndcap->CreateCaptureBuffer(&dsc,&dsndcapbuf,NULL) != DS_OK)
            return false;

        {
            DWORD fmtsz=0;
            if (dsndcapbuf->GetFormat(NULL,4096/*assume API stupidity*/,&fmtsz) != DS_OK)
                return false;
            if (fmtsz > sizeof(wfmt))
                return false;
            if (dsndcapbuf->GetFormat((WAVEFORMATEX*)(&wfmt),sizeof(wfmt),&fmtsz) != DS_OK)
                return false;

            fmt.sample_rate = wfmt.Format.nSamplesPerSec;
        }

        return true;
    }
    void dsound_force_close(void) {
        Close();
        dsound_close();
    }
    bool dsound_open(void) { // does NOT start capture
        if (!dsound_dll_init())
            return false;

        if (dsndcap == NULL) {
            GUID g;

            if (!dsound_device_string.empty()) {
                if (ans_CLSIDFromString(dsound_device_string.c_str(),&g) != S_OK)
                    return false;
            }

            if (__DirectSoundCaptureCreate(dsound_device_string.empty() ? NULL : (&g),&dsndcap,NULL) != DS_OK)
                return false;
            if (dsndcap == NULL)
                return false;

            readpos = 0;
        }

        return true;
    }
    void dsound_close(void) {
        if (dsndcapbuf != NULL) {
            dsndcapbuf->Stop();
            dsndcapbuf->Release();
            dsndcapbuf = NULL;
        }
        if (dsndcap != NULL) {
            dsndcap->Release();
            dsndcap = NULL;
        }

	buffer_size = 0;
        readpos = 0;
    }
private:
    IDirectSoundCapture*			    dsndcap;
    IDirectSoundCaptureBuffer*			dsndcapbuf;
    DWORD					            readpos;
    DWORD							buffer_size;
};

AudioSource* AudioSourceDSOUND_Alloc(void) {
    return new AudioSourceDSOUND();
}
#endif

