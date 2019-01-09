
#include "config.h"

#if defined(HAVE_DSOUND_H)
# include "dsound.h"
# include "ausrc.h"
#endif

#if defined(HAVE_DSOUND_H)
void dsound_atexit(void);
void dsound_atexit_init(void);
AudioSource* AudioSourceDSOUND_Alloc(void);
#endif

