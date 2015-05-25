/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#ifndef NOTES_H_INCLUDED
#define NOTES_H_INCLUDED

#include <time.h>

#include "wmpinboard.h"

#define draw_sketch(note) draw_sketch_area(note, 1, 1, 62, 62)

int    add_note();
void   remove_note(int);
int    raise_note(int);
int    note_tilt(int);
int    note_empty(int);
void   color_notes(int);
void   pin_note(int);
void   render_pinboard(int);
void   render_note(int);
int    selected_note(int, int);
void   print_letter(int, int, int);
void   print_text(int);
void   shift_string(int, int, int, int, int);
void   draw_sketch_area(int, int, int, int, int);
int    bbar_button(int, int);
int    abar_area(int, int);
void   init_edit_mode(int);
void   render_abar_number(int);
void   render_abar_switches(int);
void   render_abar(int);
char   *cook(int, int, int);
void   dump_notes(int);
int    char_at(int, int, int);
int    paste_string(int, int, const char*, int);
void   paste(int, int, const char*, int, int);
int    check_time();
void   explode_time(int);
void   implode_time(int);
time_t adapt_time(int, time_t);
void   time_next_alarm();

#ifdef CREASES

#include <X11/Xlib.h>

#define render_edit_wear_area(note, x, y, w, h) \
  render_edit_wear_area_win(app, note, x, y, w, h)
#define render_edit_wear(note) render_edit_wear_area(note, 1, 1, 62, 62)

void wear_note(int);
void render_wear(int);
void render_edit_wear_area_win(Window, int, int, int, int, int);

#endif

#ifdef FUNSTUFF
void check_occasion(int, int, int);
#endif

#endif  /* NOTES_H_INCLUDED */

