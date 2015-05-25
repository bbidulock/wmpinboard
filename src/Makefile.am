## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = $(X_CFLAGS)
LIBS = $(X_LIBS) -lX11 -lXext -lXpm

bin_PROGRAMS = wmpinboard

wmpinboard_SOURCES = memcmp.c \
	misc.c misc.h \
	notes.c notes.h \
	wmpinboard.c wmpinboard.h \
	xmisc.c xmisc.h
wmpinboard_CFLAGS = -g

EXTRA_wmpinboard_SOURCES = \
	getopt.c getopt1.c getopt.h \
	memcmp.c memcmp.h

