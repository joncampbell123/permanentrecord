# Main Makefile for DOSBox

EXTRA_DIST = autogen.sh
#SUBDIRS = src include

bin_PROGRAMS = \
	permrec_audio \
	streamchop

streamchop_SOURCES = streamchop.cpp

common_sources = common.cpp monclock.cpp aufmt.cpp ausrc.cpp ausrcls.cpp aufmtui.cpp dbfs.cpp autocut.cpp as_alsa.cpp as_pulse.cpp wavwrite.cpp mp3write.cpp recpath.cpp as_dsnd.cpp ole32.cpp as_wasapi.cpp

permrec_audio_SOURCES = $(common_sources) permrec_audio.cpp
permrec_audio_CXXFLAGS = $(AM_CXXFLAGS) $(AM_CFLAGS) $(ALSA_CFLAGS) $(PULSE_CFLAGS)
permrec_audio_LDFLAGS = $(AM_LDFLAGS) $(ALSA_LIBS) $(PULSE_LIBS)

if HAVE_COREAUDIO_COREAUDIO_H
permrec_audio_SOURCES += as_applecore.mm
endif

if DO_GUI_WIN32
bin_PROGRAMS += \
	permrec_audio_wingui

.rc.o:
	windres -DWINDRES=1 -o $@ $<

permrec_audio_wingui_SOURCES = $(common_sources) permrec_audio_wingui.cpp winres/permrec_audio.rc
permrec_audio_wingui_CXXFLAGS = $(permrec_audio_CXXFLAGS) -mwindows
permrec_audio_wingui_LDFLAGS = $(permrec_audio_LDFLAGS) -lgdi32 -luser32 -lkernel32
endif

if HAVE_ALSA
endif

