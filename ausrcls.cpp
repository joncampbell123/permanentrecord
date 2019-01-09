
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
#include "audev.h"
#include "ausrcls.h"

#include "as_alsa.h"
#include "as_pulse.h"
#include "as_dsnd.h"

#if defined(HAVE_ALSA)
AudioSource* AudioSourceALSA_Alloc(void);
#endif

#if defined(HAVE_PULSE)
AudioSource* AudioSourcePULSE_Alloc(void);
#endif

#if defined(HAVE_DSOUND_H)
AudioSource* AudioSourceDSOUND_Alloc(void);
#endif

const AudioSourceListEntry audio_source_list[] = {
#if defined(HAVE_PULSE)
    {"PULSE",
     "PulseAudio",
     &AudioSourcePULSE_Alloc},
#endif
#if defined(HAVE_ALSA)
    {"ALSA",
     "Linux Advanced Linux Sound Architecture",
     &AudioSourceALSA_Alloc},
#endif
#if defined(HAVE_DSOUND_H)
    {"DSOUND",
     "Microsoft DirectSound (DirectX)",
     &AudioSourceDSOUND_Alloc},
#endif


    {NULL,
     NULL,
     NULL}
};

const audiosourcealloc_t default_source_order[] = {
#if defined(HAVE_ALSA)
    &AudioSourceALSA_Alloc,
#endif
#if defined(HAVE_PULSE)/*PulseAudio has weird latency issues with my sound card, imposing a 32768 byte fragment size???*/
    &AudioSourcePULSE_Alloc,
#endif
#if defined(HAVE_DSOUND_H)
    &AudioSourceDSOUND_Alloc,
#endif
    NULL
};

AudioSource* GetAudioSource(const char *src/*must not be NULL*/) {
    size_t i=0;

    if (*src == 0)
        return PickDefaultAudioSource();

    for (i=0;audio_source_list[i].name != NULL;i++) {
        if (!strcasecmp(src,audio_source_list[i].name))
            return audio_source_list[i].alloc();
    }

    return NULL;
}

AudioSource* PickDefaultAudioSource(void) {
    const audiosourcealloc_t *s = default_source_order;

    while (*s != NULL) {
        AudioSource *src = (*s)();

        if (src != NULL)
            return src;

        s++;
    }

    return NULL;
}

