/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#ifndef XMISC_H_INCLUDED
#define XMISC_H_INCLUDED

#include <X11/Xlib.h>

#include "wmpinboard.h"

void replace_color(XImage*, int, int, int, int, unsigned long, unsigned long);
void merge_masked(XImage*, int, int, int, int, int, int, unsigned long);

void get_xpm_with_mask(char**, Pixmap*, Pixmap*);
Pixmap get_xpm(char**);
void flush_expose(void);
void redraw_window(void);
void set_mask(int);
void init_xlocale(void);
Window create_win(void);
void cb_copy(const char*, int);
void handle_SelectionRequest(XSelectionRequestEvent *rq);
void cb_paste(int, int);
void cb_paste_external(Window, unsigned, int, int, int, int);
void cb_clear(void);
void prepare_alarm_anim();

extern XIC InputContext;

#endif  /* XMISC_H_INCLUDED */

