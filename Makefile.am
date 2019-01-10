# Main Makefile for DOSBox

EXTRA_DIST = autogen.sh
#SUBDIRS = src include

bin_PROGRAMS = \
	permrec_audio

permrec_audio_SOURCES = permrec_audio.cpp common.cpp monclock.cpp aufmt.cpp ausrc.cpp ausrcls.cpp aufmtui.cpp dbfs.cpp autocut.cpp as_alsa.cpp as_pulse.cpp wavwrite.cpp recpath.cpp as_dsnd.cpp ole32.cpp as_wasapi.cpp
permrec_audio_CXXFLAGS = $(AM_CXXFLAGS) $(AM_CFLAGS) $(ALSA_CFLAGS) $(PULSE_CFLAGS)
permrec_audio_LDFLAGS = $(AM_LDFLAGS) $(ALSA_LIBS) $(PULSE_LIBS)

if HAVE_ALSA
endif

