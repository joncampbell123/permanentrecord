dnl Init.
AC_INIT(permanentrecord,0.0.1,[],[],[])
AC_PREREQ(2.60)
AC_CONFIG_SRCDIR(README)

dnl Utility function ============================

# AC_CHECK_CPPFLAGS(ADDITIONAL-CPPFLAGS, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
#
# checks whether the $(C) compiler accepts the ADDITIONAL-CPPFLAGS
# if so, they are added to the CPPFLAGS
AC_DEFUN([AC_CHECK_CPPFLAGS],
[
  AC_MSG_CHECKING([whether $CPP accepts "$1"])
  temp_check_cppflags="${CPPFLAGS}"
  CPPFLAGS="$1 ${CPPFLAGS}"
  AC_PREPROC_IFELSE(
        [AC_LANG_SOURCE([[int main(void){return 0;}]])],
        [AC_MSG_RESULT([yes])],
        [AC_MSG_RESULT([no]); CPPFLAGS="${temp_check_cppflags}"])
])# AC_CHECK_CPPFLAGS

dnl Utility function ============================

# AC_CHECK_CXXFLAGS(ADDITIONAL-CXXFLAGS, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
#
# checks whether the $(CXX) (c++) compiler accepts the ADDITIONAL-CXXFLAGS
# if so, they are added to the CXXFLAGS
AC_DEFUN([AC_CHECK_CXXFLAGS],
[
  AC_MSG_CHECKING([whether $CXX accepts "$1"])
cat > conftest.c++ << EOF
int main(){
  return 0;
}
EOF
if $CXX $CPPFLAGS $CXXFLAGS -o conftest.o conftest.c++ [$1] > /dev/null 2>&1
then
  AC_MSG_RESULT([yes])
  CXXFLAGS="${CXXFLAGS} [$1]"
  AC_CHECK_CPPFLAGS([$1])
  [$2]
else
  AC_MSG_RESULT([no])
  [$3]
fi
])# AC_CHECK_CXXFLAGS

dnl End Utility function ============================

dnl Detect the canonical host and target build environment
AC_CANONICAL_HOST
AC_CANONICAL_BUILD

dnl Setup for automake
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_HEADER(config.h)

dnl Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_OBJCXX
AC_PROG_INSTALL
AC_PROG_RANLIB
AC_PREFIX_DEFAULT([/usr/local])

dnl this code needs large file support on 32-bit systems
AC_SYS_LARGEFILE

#Check for big endian machine, should #define WORDS_BIGENDIAN if so
AC_C_BIGENDIAN

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_CHECK_SIZEOF(unsigned char)
AC_CHECK_SIZEOF(unsigned short)
AC_CHECK_SIZEOF(unsigned int)
AC_CHECK_SIZEOF(unsigned long)
AC_CHECK_SIZEOF(unsigned long long)
AC_CHECK_SIZEOF(int *)

AC_CHECK_HEADERS([endian.h])
AC_CHECK_HEADERS([machine/endian.h])
AC_CHECK_HEADERS([CoreAudio/CoreAudio.h])

if test "x$ac_cv_header_endian_h" != xyes; then
    CXXFLAGS="-I"'$(abs_top_srcdir)'"/fillin/endian $CXXFLAGS"
    OBJCXXFLAGS="-I"'$(abs_top_srcdir)'"/fillin/endian $OBJCXXFLAGS"
fi

if test "x$ac_cv_header_CoreAudio_CoreAudio_h" = xyes; then
    LDFLAGS="-framework CoreAudio -framework AudioToolbox -framework CoreFoundation $LDFLAGS";
fi

AM_CONDITIONAL(HAVE_COREAUDIO_COREAUDIO_H, test "x$ac_cv_header_CoreAudio_CoreAudio_h" = xyes )

dnl some semi complex check for sys/socket so it works on darwin as well
AC_CHECK_HEADERS([stdlib.h sys/types.h])
AC_CHECK_HEADERS([sys/socket.h  netinet/in.h pwd.h], [], [],
[#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
])

dnl check for the socklen_t (darwin doesn't always have it)
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
])],[],[AC_DEFINE([socklen_t],[int],[Define to `int` if you don't have socklen_t])])

AC_MSG_CHECKING([checking for C++11 support])
AC_CHECK_CXXFLAGS([ -std=gnu++11 ], [ checkagain=0 ], [ checkagain=1 ])
if test x$checkagain = x1; then
AC_CHECK_CXXFLAGS([ -std=gnu++0x ], [ checkagain=0 ], [ checkagain=1 ])
fi

dnl FIXME: Remove default "-O2" set by some autotools versions. TODO: check availability of sed.
CFLAGS=["`echo $CFLAGS' ' | sed -e 's/-O[^ ]* //g'`"]
CXXFLAGS=["`echo $CXXFLAGS' ' | sed -e 's/-O[^ ]* //g'`"]

CFLAGS="$CFLAGS -O2"
CXXFLAGS="$CXXFLAGS -O2"

case "$host" in
    *-*-cygwin* | *-*-mingw32*)
    # MinGW please do not make our EXE dependent on MinGW DLLs
    CXXFLAGS="$CXXFLAGS -static -static-libgcc"
    LDFLAGS="$LDFLAGS -static -static-libgcc"
    enable_alt_dsound=yes
    gui_win32=yes
    ;;
esac

AM_CONDITIONAL(DO_GUI_WIN32, test "x$gui_win32" = xyes)

dnl I would like to know of any concerns given by the C++ compiler.
dnl Clang/LLVM already does this to some degree, let's get GCC to do it too.
AC_CHECK_CXXFLAGS([ -Wall ])
AC_CHECK_CXXFLAGS([ -Wextra ])
AC_CHECK_CXXFLAGS([ -pedantic ])
AC_CHECK_CXXFLAGS([ -Wconversion ])
AC_CHECK_CXXFLAGS([ -Wsign-conversion ])
AC_CHECK_CXXFLAGS([ -Wconversion-null ])

dnl Let GCC 7.3.x know that the "fall through" switch cases in this codebase
dnl are often intentional.
AC_CHECK_CXXFLAGS([ -Wno-implicit-fallthrough ])

dnl Stop reporting "type punning" warnings, I'm sick of hearing about it.
AC_CHECK_CXXFLAGS([ -Wno-strict-aliasing ])

dnl other
AC_CHECK_CXXFLAGS([ -Wno-missing-field-initializers ])
AC_CHECK_CXXFLAGS([ -Wno-format-zero-length ])

dnl Clang/LLVM warning: don't care the address of a member may be unaligned, unless targeting ARM!
AC_CHECK_CXXFLAGS([ -Wno-address-of-packed-member ])
dnl Clang/LLVM warning: don't care about int to void*, since void* is either same size or larger
AC_CHECK_CXXFLAGS([ -Wno-int-to-void-pointer-cast ])
dnl Clang/LLVM warning: extended field designator
AC_CHECK_CXXFLAGS([ -Wno-extended-offsetof ])

dnl library support

dnl --- Do you have the LAME MP3 encoder library? Do you want it?
AC_CHECK_HEADER([lame/lame.h], [has_lame=yes], [has_lame=no])
AC_ARG_ENABLE(lame,AC_HELP_STRING([--enable-lame],[Enable LAME MP3 encoder support (yes by default)]),enable_lame=$enableval,enable_lame=yes)
if test x"$has_lame" == x"yes" -a x"$enable_lame" == x"yes"; then
    AC_DEFINE(HAVE_LAME, 1, [Has LAME library])
    choice=1
else
    choice=0
fi
AM_CONDITIONAL(HAVE_LAME, test $choice = 1)

dnl --- Do you have ALSA? Do you want it?
x=`pkg-config alsa --exists`; res=$?;
if test $res = 0; then
    has_alsa=yes
    r=`pkg-config alsa --cflags`
    CXXFLAGS="$CXXFLAGS $r"
    r=`pkg-config alsa --libs`
    LDFLAGS="$LDFLAGS $r"
fi
AC_ARG_ENABLE(alsa,AC_HELP_STRING([--enable-alsa],[Enable ALSA support (yes by default)]),enable_alsa=$enableval,enable_alsa=yes)
if test x"$has_alsa" == x"yes" -a x"$enable_alsa" == x"yes"; then
    AC_DEFINE(HAVE_ALSA, 1, [Has ALSA library])
    choice=1
else
    choice=0
fi
AM_CONDITIONAL(HAVE_ALSA, test $choice = 1)

dnl --- Do you have PULSE? Do you want it?
x=`pkg-config libpulse --exists`; res=$?;
if test $res = 0; then
    has_pulse=yes
    r=`pkg-config libpulse --cflags`
    CXXFLAGS="$CXXFLAGS $r"
    r=`pkg-config libpulse --libs`
    LDFLAGS="$LDFLAGS $r"
fi
AC_ARG_ENABLE(pulse,AC_HELP_STRING([--enable-pulse],[Enable PulseAudio support (yes by default)]),enable_pulse=$enableval,enable_pulse=yes)
if test x"$has_pulse" == x"yes" -a x"$enable_pulse" == x"yes"; then
    AC_DEFINE(HAVE_PULSE, 1, [Has PULSE library])
    choice=1
else
    choice=0
fi
AM_CONDITIONAL(HAVE_PULSE, test $choice = 1)

dnl -- Do you want multithreading?
AC_ARG_ENABLE(threads,AC_HELP_STRING([--enable-threads],[Enable multi-threaded code]),enable_threads=$enableval,enable_threads=yes)

dnl -- Do you have pthreads? Do you want it?
AC_CHECK_CXXFLAGS([ -pthread ],has_pthreads=yes,has_pthreads=no)
AC_ARG_ENABLE(pthreads,AC_HELP_STRING([--enable-pthreads],[Enable pthread support]),enable_pthreads=$enableval,enable_pthreads=yes)
if test x"$has_pthreads" == x"yes" -a x"$enable_pthreads" == x"yes" -a x"$enable_threads" == x"yes"; then
    AC_DEFINE(HAVE_PTHREADS, 1, [Has pthreads library])
    choice=1
else
    choice=0
fi
AM_CONDITIONAL(HAVE_PTHREADS, test $choice = 1)

# directsound?
if test x"$enable_alt_dsound" == x"yes" -a x"$have_dsound" != x"yes"; then
    CXXFLAGS="-I"'$(abs_top_srcdir)'"/fillin/dsound $CXXFLAGS"
    have_dsound=yes
    AC_DEFINE(HAVE_DSOUND_H, 1, [ Define to 1 to enable DirectSound support ])
else
    AC_CHECK_HEADERS([dsound.h], have_dsound=yes, , )
fi
if test x"$have_dsound" = x"yes"; then
    LDFLAGS="$LDFLAGS -ldsound"
    AC_DEFINE(C_DSOUND, 1, [ Define to 1 to enable DirectSound support ])
fi

# wasapi?
AC_CHECK_HEADERS([mmdeviceapi.h], have_mmdeviceapi=yes, , )
if test x"$have_mmdeviceapi" = x"yes"; then
    LDFLAGS="$LDFLAGS"
    AC_DEFINE(C_WASAPI, 1, [ Define to 1 to enable Windows Session API support ])
fi

# clock_gettime?
AC_CHECK_LIB(rt, clock_gettime, have_clock_gettime=yes, , )
if test x"$have_clock_gettime" = x"yes"; then
    LDFLAGS="$LDFLAGS -lrt -lm"
    AC_DEFINE(C_CLOCK_GETTIME, 1, [ Define to 1 if clock_gettime() is available. ])
fi

dnl --- done
AC_CONFIG_FILES([ 
Makefile
])
AC_OUTPUT

