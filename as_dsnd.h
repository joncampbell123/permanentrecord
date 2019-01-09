
#include "config.h"

#if defined(HAVE_DSOUND)
# define DSOUND_PCM_NEW_HW_PARAMS_API
# include <dsound.h>
# include "ausrc.h"
#endif

#if defined(HAVE_DSOUND)
void dsound_atexit(void);
void dsound_atexit_init(void);
AudioSource* AudioSourceDSOUND_Alloc(void);
#endif

