
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
# include <windows.h>
# include <objbase.h>

extern bool ole32_atexit_set;
extern HMODULE ole32_dll;

extern HRESULT (WINAPI *__CoCreateInstance)(REFCLSID rclsid,LPUNKNOWN pUnkOuter,DWORD dwClsContext,REFIID riid,LPVOID *ppv);
extern int (WINAPI *__StringFromGUID2)(REFGUID rguid,LPOLESTR lpsz,int cchMax);
extern HRESULT (WINAPI *__CLSIDFromString)(LPOLESTR lpsz,LPCLSID pclsid);
extern HRESULT (WINAPI *__CoInitialize)(LPVOID pvReserved);
extern void (WINAPI *__CoTaskMemFree)(LPVOID pv);
extern void (WINAPI *__CoUninitialize)();

void ole32_atexit(void);
void ole32_atexit_init(void);
bool ole32_dll_init(void);
void OLEToCharConvertInPlace(char *sz,int cch);
HRESULT ans_CLSIDFromString(const char *sz,LPCLSID pclsid);
int ans_StringFromGUID2(REFGUID rguid,char *sz,int cchMax);
void ole32_couninit(void);
bool ole32_coinit(void);
#endif

