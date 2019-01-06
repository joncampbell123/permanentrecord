
#include "config.h"

#if defined(HAVE_ALSA)
# define ALSA_PCM_NEW_HW_PARAMS_API
# include <alsa/asoundlib.h>
# include "ausrc.h"
#endif

#if defined(HAVE_ALSA)
void alsa_atexit(void);
void alsa_atexit_init(void);
AudioSource* AudioSourceALSA_Alloc(void);
#endif

