## Process this file with automake to produce Makefile.in

INCLUDES = $(X_CFLAGS)
LIBS = $(X_LIBS) -lX11 -lXext -lXpm

bin_PROGRAMS = wmpinboard

wmpinboard_SOURCES = @CONDITIONAL_SOURCES@ \
	misc.c misc.h \
	notes.c notes.h \
	wmpinboard.c wmpinboard.h \
	xmisc.c xmisc.h

EXTRA_wmpinboard_SOURCES = \
	getopt.c getopt1.c getopt.h \
	memcmp.c memcmp.h

