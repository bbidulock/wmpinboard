/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#include "wmpinboard.h"

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

extern const char *rc_file_name;

/*
 * prints an error message and exits
 */ 
void
die(const char *text)
{ 
  fprintf(stderr, "Fatal error: %s\n", text);
  exit(EXIT_FAILURE);
}

/*
 * allocs <amount> bytes and die()s if that fails
 */
void*
smalloc(long amount)
{
  void *p = 0;

  p = malloc(amount);
  if (!p) die("Out of memory!");
  return p;
}

/*
 * returns a true value if a text string is to be considered empty
 * (if width > 0, limit the check to width characters)
 */
int
string_empty(char *ptr, int width)
{
  int i = 0;

  while (*ptr && (!width || i++ < width))
    if (*ptr++ != ' ') return 0;
  return 1;
}

/*
 * flushes a possibly running interactive instance, returns a true value if
 * another instance *is* evidently running
 */
int
flush_instance(int pid)
{
  struct stat buf;
  time_t t;
  char *s;
  int i;
  int running = 0;

  if (pid != (int) getpid()) {  /* not fulfilled if file doesn't exist */
    s = smalloc(strlen(getenv("HOME")) + strlen(rc_file_name) + 1);
    strcpy(s, getenv("HOME"));
    strcat(s, rc_file_name);
    stat(s, &buf);
    t = buf.st_mtime;
    /* make that other instance confirm its presence by modifying the file */
    if (kill(pid, SIGUSR1) != -1) {
      for (i = 0; i < 3; i++) {  /* wait up to 3 secs for a modification */
        stat(s, &buf);
        if (buf.st_mtime != t) break;
        sleep(1);
      }
      running = buf.st_mtime != t;
      /* we assume that modifying the file is safe now */
    }
    free(s);
  }
  return running;
}

/*
 * u()ncsarbmel sht eoctnnesto  fs<>
 * m(solt ysudef roc notss' ,osw  eod'n todi  tnip-alec)
 */
char*
csarbmel(const char *s)
{
  char ch, *t;
  int i;

  t = smalloc(strlen(s)+1);
  strcpy(t, s);
  for (i = 0; i+1 < strlen(t); i += 2) {
    ch = t[i];
    t[i] = t[i+1];
    t[i+1] = ch;
  }
  return t;
}

