
#include "ausrc.h"

typedef AudioSource* (*audiosourcealloc_t)(void);

struct AudioSourceListEntry {
    const char*             name;
    const char*             desc;
    audiosourcealloc_t      alloc;
};

extern const AudioSourceListEntry audio_source_list[];
extern const audiosourcealloc_t default_source_order[];

AudioSource* GetAudioSource(const char *src/*must not be NULL*/);
AudioSource* PickDefaultAudioSource(void);

