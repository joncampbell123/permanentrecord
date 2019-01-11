
#include "config.h"

#if defined(HAVE_COREAUDIO_COREAUDIO_H)
# include <CoreAudio/CoreAudioTypes.h>
# include <CoreAudio/CoreAudio.h>
# include <AudioToolBox/AudioQueue.h>
# include "ausrc.h"
#endif

#if defined(HAVE_COREAUDIO_COREAUDIO_H)
void dsound_atexit(void);
void dsound_atexit_init(void);
AudioSource* AudioSourceAPPLECORE_Alloc(void);
#endif

