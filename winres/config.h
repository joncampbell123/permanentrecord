/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
#undef AC_APPLE_UNIVERSAL_BUILD

/* Define to 1 if clock_gettime() is available. */
#undef C_CLOCK_GETTIME

/* Define to 1 to enable DirectSound support */
#define C_DSOUND

/* Define to 1 to enable Windows Session API support */
#define C_WASAPI

/* Has ALSA library */
#undef HAVE_ALSA

/* Define to 1 if you have the <CoreAudio/CoreAudio.h> header file. */
#undef HAVE_COREAUDIO_COREAUDIO_H

/* Define to 1 if you have the <dsound.h> header file. */
#define HAVE_DSOUND_H

/* Define to 1 if you have the <endian.h> header file. */
#undef HAVE_ENDIAN_H

/* Define to 1 if you have the <inttypes.h> header file. */
#undef HAVE_INTTYPES_H

/* Has LAME library */
#undef HAVE_LAME

/* Define to 1 if you have the <machine/endian.h> header file. */
#undef HAVE_MACHINE_ENDIAN_H

/* Define to 1 if you have the <memory.h> header file. */
#undef HAVE_MEMORY_H

/* Define to 1 if you have the <mmdeviceapi.h> header file. */
#define HAVE_MMDEVICEAPI_H

/* Define to 1 if you have the <netinet/in.h> header file. */
#undef HAVE_NETINET_IN_H

/* Has OGG library */
#undef HAVE_OGG

/* Has OPUS library */
#undef HAVE_OPUS

/* Has OPUSENC library */
#undef HAVE_OPUSENC

/* Has pthreads library */
#undef HAVE_PTHREADS

/* Has PULSE library */
#undef HAVE_PULSE

/* Define to 1 if you have the <pwd.h> header file. */
#undef HAVE_PWD_H

/* Define to 1 if you have the <stdint.h> header file. */
#undef HAVE_STDINT_H

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H

/* Define to 1 if you have the <strings.h> header file. */
#undef HAVE_STRINGS_H

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H

/* Define to 1 if you have the <sys/socket.h> header file. */
#undef HAVE_SYS_SOCKET_H

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H

/* Define to 1 if you have the <unistd.h> header file. */
#undef HAVE_UNISTD_H

/* Has VORBIS library */
#undef HAVE_VORBIS

/* Has VORBISENC library */
#undef HAVE_VORBISENC

/* Name of package */
#undef PACKAGE

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
#undef TM_IN_SYS_TIME

/* Version number of package */
#undef VERSION

#if defined(_MSC_VER)
# if !defined(GOD_DAMN_MOTHERFUCKING_VS2019_POSIX_HACKS)
#  define GOD_DAMN_MOTHERFUCKING_VS2019_POSIX_HACKS
#  include <direct.h>
/* Microsoft thinks POSIX names are, like, soooooooooooooo old that nobody except old stinky UNIX farts should ever use them and you have to waste your time putting an underscore before them */
#  define strcasecmp _strcmpi
#  define close _close
#  define lseek _lseek
#  define mkdir _mkdir
#  define open _open
#  define write _write
#  define read _read
#  include <windows.h>

static inline void usleep(const unsigned long t) {
	Sleep((DWORD)(t / 1000ul)/* microsecond to millisecond*/);
}
# endif
#endif