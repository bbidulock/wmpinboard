/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include "wmpinboard.h"

#define WARN(msg) fprintf(stderr, "Warning: %s\n", msg)

void die(const char*);
void *smalloc(long);
int  string_empty(char*, int);
int  flush_instance(int);
char *csarbmel(const char *);
void block_sigs();
void unblock_sigs();

#endif  /* MISC_H_INCLUDED */

