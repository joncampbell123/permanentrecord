
#include "config.h"

#if defined(HAVE_MMDEVICEAPI_H)
# include "mmdeviceapi.h"
# include "audioclient.h"
# include "ausrc.h"
#endif

#if defined(HAVE_MMDEVICEAPI_H)
void dsound_atexit(void);
void dsound_atexit_init(void);
AudioSource* AudioSourceWASAPI_Alloc(void);
#endif

