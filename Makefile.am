# Main Makefile for DOSBox

EXTRA_DIST = autogen.sh
#SUBDIRS = src include

bin_PROGRAMS = \
	test

test_SOURCES = test.cpp
test_CXXFLAGS = $(AM_CXXFLAGS) $(AM_CFLAGS) $(ALSA_CFLAGS)
test_LDFLAGS = $(AM_LDFLAGS) $(ALSA_LIBS)

if HAVE_ALSA
endif

