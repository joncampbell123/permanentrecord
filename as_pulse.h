
#include "config.h"

#if defined(HAVE_PULSE)
# include <pulse/pulseaudio.h>
# include <pulse/error.h>
# include "ausrc.h"
#endif

#if defined(HAVE_PULSE)
void pulse_atexit(void);
void pulse_atexit_init(void);
AudioSource* AudioSourcePULSE_Alloc(void);
#endif

