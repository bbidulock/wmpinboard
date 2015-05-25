dnl Process this file with autoconf to produce a configure script.

AC_INIT([wmpinboard], [1.0])
AM_INIT_AUTOMAKE([-Wall silent-rules])
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
dnl don't want the -g flag
CFLAGS=`echo "x $CFLAGS"|sed 's/^x //; s/\(^\| \)-g\($\| \)/ /g'`
AC_ISC_POSIX
AC_LANG_C

dnl use POSIX and BSD stuff where available
CFLAGS="$CFLAGS -D_DEFAULT_SOURCE -D_POSIX_SOURCE=199506L"

dnl additional compile flags when using gcc
if test x$GCC = xyes; then
  CFLAGS="$CFLAGS -Wall -ansi -pedantic"
fi

dnl X11 stuff
AC_PATH_XTRA
AC_CHECK_LIB(Xpm, main, AC_DEFINE([HAVE_LIBXPM], 1,
  [define if the XPM library is available]),
  AC_MSG_ERROR(The XPM library is required but wasn't found.),
  $X_LIBS -lX11 -lXext)

AC_CHECK_LIB(ife, sense)

AC_C_CONST
AC_C_INLINE
dnl check for C headers and functions
dnl more or less optional ones first
AC_CHECK_HEADERS([getopt.h malloc.h signal.h string.h unistd.h])
AC_CHECK_FUNC(getopt, , need_getopt=yes)
AC_CHECK_FUNC(getopt_long, , need_getopt_long=yes)
AC_FUNC_MEMCMP
if test x"$ac_cv_func_memcmp_clean" = xyes; then
  AC_DEFINE([HAVE_MEMCMP], 1,
  [define if there is an 8-bit-clean memcmp()])
else
  need_memcmp=yes
fi
dnl required ones
AC_CHECK_FUNC(strchr, ,
  AC_MSG_ERROR(Your C library seems to be lacking the strchr() function.))
AC_CHECK_FUNC(strcasecmp, ,
  AC_MSG_ERROR(Your C library seems to be lacking the strcasecmp() function.))
AC_CHECK_FUNC(ualarm, ,
  AC_MSG_ERROR(Your C library seems to be lacking the ualarm() function.))
AC_CHECK_FUNC(usleep, ,
  AC_MSG_ERROR(Your C library seems to be lacking the usleep() function.))
AC_CHECK_FUNC(kill, ,
  AC_MSG_ERROR(Your C library seems to be lacking the kill() function.))

dnl === compile-time options ===
dnl TIMEOUT
AC_ARG_ENABLE(timeout,
[  --enable-timeout[=TIME] set edit mode timeout to TIME seconds (default = 60)],
[case "${enableval}" in
	yes)	timeout=60 ;;
	no)		timeout=0 ;;
	*)		timeout=`echo "${enableval}"|sed 's/[^0-9]//g'`
			if test x"$timeout" = x; then
				AC_MSG_ERROR(Bad value ${enableval} for --enable-timeout.)
			fi
			if test $timeout -gt 0 -a $timeout -lt 5; then
				timeout=5
				AC_MSG_WARN(Timeout value to low: increasing to 5 seconds.)
			fi
			;;
esac], [timeout=60])
AC_DEFINE_UNQUOTED(TIMEOUT, ${timeout},
  [define the edit mode timeout in seconds (0 disables)])

dnl CREASES
AC_ARG_ENABLE(creases,
[  --enable-creases        enable wear and tear (creases) (default = yes)],
[case "${enableval}" in
	yes)	creases=yes ;;
	no)		creases=no ;;
	*)		AC_MSG_ERROR(Bad value ${enableval} for --enable-creases.) ;;
esac], [creases=yes])
if test x"$creases" = xyes; then
  AC_DEFINE([CREASES], 1,
  [define if wear & tear of notes (creases) is to be simulated])
fi

dnl determine what conditional stuff has to be compiled
if test x"$need_memcmp" = xyes; then
  CONDITIONAL_SOURCES="$CONDITIONAL_SOURCES memcmp.c"
fi
if test x"$need_getopt" = xyes; then
  CONDITIONAL_SOURCES="$CONDITIONAL_SOURCES getopt.c"
fi
if test x"$need_getopt_long" = xyes; then
  CONDITIONAL_SOURCES="$CONDITIONAL_SOURCES getopt1.c"
fi
AC_SUBST(CONDITIONAL_SOURCES)

AC_OUTPUT([
Makefile
src/Makefile
man/Makefile
wmpinboard.spec
])

