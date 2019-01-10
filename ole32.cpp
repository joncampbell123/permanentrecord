
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

#if defined(WIN32)
# include "monclock.h"
# include "aufmt.h"
# include "aufmtui.h"
# include "audev.h"
# include "ausrc.h"
# include "ausrcls.h"
# include "dbfs.h"
# include "autocut.h"
# include "wavstruc.h"
# include "ole32.h"

bool ole32_has_coinit = false;
bool ole32_atexit_set = false;
HMODULE ole32_dll = NULL;

int (WINAPI *__StringFromGUID2)(REFGUID rguid,LPOLESTR lpsz,int cchMax) = NULL;
HRESULT (WINAPI *__CLSIDFromString)(LPOLESTR lpsz,LPCLSID pclsid) = NULL;
HRESULT (WINAPI *__CoInitialize)(LPVOID pvReserved) = NULL;
void (WINAPI *__CoUninitialize)() = NULL;

void ole32_atexit(void) {
    if (ole32_dll != NULL) {
        FreeLibrary(ole32_dll);
        ole32_dll = NULL;
    }
}

void ole32_couninit(void) {
    if (ole32_has_coinit) {
        __CoUninitialize();
        ole32_has_coinit = false;
    }
}

void ole32_atexit_init(void) {
    ole32_couninit();
    if (!ole32_atexit_set) {
        ole32_atexit_set = 1;
        atexit(ole32_atexit);
    }
}

bool ole32_dll_init(void) {
    if (ole32_dll == NULL) {
        if ((ole32_dll=LoadLibrary("OLE32.DLL")) == NULL)
            return false;

        __StringFromGUID2 =
            (int (WINAPI*)(REFGUID,LPOLESTR,int))
            GetProcAddress(ole32_dll,"StringFromGUID2");
        if (__StringFromGUID2 == NULL)
            return false;

        __CLSIDFromString =
            (HRESULT (WINAPI*)(LPOLESTR,LPCLSID))
            GetProcAddress(ole32_dll,"CLSIDFromString");
        if (__CLSIDFromString == NULL)
            return false;

        __CoInitialize =
            (HRESULT (WINAPI*)(LPVOID))
            GetProcAddress(ole32_dll,"CoInitialize");
        if (__CoInitialize == NULL)
            return false;

        __CoUninitialize =
            (void (WINAPI*)())
            GetProcAddress(ole32_dll,"CoUninitialize");
        if (__CoUninitialize == NULL)
            return false;
    }

    return true;
}

bool ole32_coinit(void) {
    if (!ole32_dll_init())
        return false;

    if (!ole32_has_coinit) {
        HRESULT hr;

        hr = __CoInitialize(NULL);
        if (hr == S_OK || hr == S_FALSE)
            ole32_has_coinit = true;
        else
            return false;
    }

    return true;
}

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
HRESULT ans_CLSIDFromString(const char *sz,LPCLSID pclsid) {
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
int ans_StringFromGUID2(REFGUID rguid,char *sz,int cchMax) {
    int r;

    r = __StringFromGUID2(rguid,(LPOLESTR)sz,/*size from chars to wchar_t of buffer*/(int)((unsigned int)cchMax / sizeof(wchar_t)));
    /* r = chars including NULL terminator (bytes is r * sizeof(wchar_t) */
    OLEToCharConvertInPlace(sz,r);
    return r;
}

#endif

