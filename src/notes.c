/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#include <stdlib.h>
#include <stdio.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "wmpinboard.h"
#include "misc.h"
#include "xmisc.h"
#include "notes.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifndef HAVE_MEMCMP
#include "memcmp.h"
#endif

/* a list of the upper left corners of the separate areas of the alarm panel,
   numbered from left to right and from top to bottom (double digits followed
   by  switches) */
typedef struct { int x, y; } coords_t;
const coords_t abar_areas[6] = {
  /* double digits, 12x9 each */
  { 3, 4 }, { 19, 4 },
  { 3, 15 }, { 19, 15 },
  /* switches, 8x8 each */
  { 47, 4 }, { 47, 16 }
};

/*
 * adds a new note and returns its number (or -1 if notes limit exceeded)
 */
int
add_note()
{
  int cols[C_NUM];
  int rep = 20;
  int i, j, n;

  if (notes_count >= MAX_NOTES)
    return -1;
  n = notes_count++;

  /* find a reasonable color */
  for (i = 0; i < C_NUM; i++) cols[i] = 1;
  for (i = 0; i < n; i++) cols[ndata[i].col] = 0;
  for (i = j = 0; i < C_NUM; i++)
    if (cols[i]) cols[j++] = i;
  ndata[n].col = j ? cols[rand() % j] : rand() % C_NUM;

  /* choose a random place to pin it, some pixels off any other note's origin
     (possible if MAX_NOTES is sufficiently small) */
  do {
    ndata[n].x = 6 + rand() % 36;
    ndata[n].y = 2 + rand() % 44;
    j = 1;
    for (i = 0; i < n; i++)
      if (ndata[i].x - ndata[n].x < 5 && ndata[i].x - ndata[n].x > -5 &&
          ndata[i].y - ndata[n].y < 5 && ndata[i].y - ndata[n].y > -5)
      {
        j = 0;
        break;
      }
  } while (!j && --rep);

  memset(ndata[n].text, 32, 59);
  ndata[n].text[59] = '\0';
  ndata[n].cursor = 0;
  memset(ndata[n].sketch, 0, 512);
  memset(ndata[n].creases, 0, 32);
  ndata[n].a_time = -1;
  ndata[n].a_flags = ALARM_DATE;

  state.counter++;

  return n;
}

/*
 * removes a note
 */
void
remove_note(int number)
{
  int i;

  if (number >= notes_count) return;
  /* in case memcpy() can't do overlapping copies... */
  for (i = number; i < notes_count-1; i++)
    memcpy(&ndata[i], &ndata[i+1], sizeof(data_t));
  notes_count--;
}

/*
 * moves a note on top of all others and returns the quote's new ID
 */
int
raise_note(int number)
{
  data_t tmp;
  int i;

  if (number >= notes_count-1 || number < 0) return number;
  memcpy(&tmp, &ndata[number], sizeof(data_t));
  /* in case memcpy() can't do overlapping copies... */
  for (i = number; i < notes_count-1; i++)
    memcpy(&ndata[i], &ndata[i+1], sizeof(data_t));
  memcpy(&ndata[notes_count-1], &tmp, sizeof(data_t));
  return notes_count-1;
}

/*
 * returns a value corresponding to how tilted a note is to be displayed
 */
inline int
note_tilt(int note)
{
  return ndata[note].x < 14 ? 0 : (ndata[note].x < 36 ? 1 : 2);
}

/*
 * returns true if the note in question is to be considered empty
 */
int
note_empty(int note)
{
  char *ptr = ndata[note].sketch;
  int i;

  if (!string_empty(ndata[note].text, 0)) return 0;
  for (i = 0; i < 511; i++, ptr++)  /* last byte ignored */
    if (*ptr) return 0;
  return 1;
}

/*
 * creates templates for the icons suiting the specified note
 */
void
color_notes(int note)
{
  XGetSubImage(display, app, 64, 0, 16, 48, ~0, ZPixmap, img, 64, 0);
  if (ndata[note].sketch[511] & 1)
    replace_color(img, 64, 0, 16, 48, C_EXTRA, palette[ndata[note].col].fg);
  else
    replace_color(img, 64, 0, 16, 48, C_EXTRA, palette[ndata[note].col].bg);
  replace_color(img, 64, 0, 16, 48, C_INNER, palette[ndata[note].col].bg);
#ifdef CREASES
  render_wear(note);
#endif
}

/*
 * draws a note's miniature representation on the pinboard
 */
void
pin_note(int note)
{
  merge_masked(img, 64, 16*note_tilt(note), ndata[note].x,
    ndata[note].y, 16, 16, C_OUTER);
}

/*
 * renders the pinboard with all of its notes excluding <exclude> if >= 0
 */
void
render_pinboard(int exclude)
{
  int i;

  XPutImage(display, app, normalGC, img, 0, 0, 0, 0, 64, 64);
  for (i = 0; i < notes_count; i++)
    if (i != exclude) {
      color_notes(i);
      pin_note(i);
    }
}

/*
 * if (<x>, <y>) is within a quote's area, returns the quote's ID, otherwise -1
 */
int
selected_note(int x, int y)
{
  int i;

  for (i = notes_count-1; i >= 0; i--)
    if (x >= ndata[i].x && x < ndata[i].x+16 &&
        y >= ndata[i].y && y < ndata[i].y+16 &&
        XGetPixel(img, 128+x-ndata[i].x, note_tilt(i)*16+y-ndata[i].y) !=
          C_OUTER)
    {
      return i;
    }
  return -1;
}

/*
 * prints a letter identified by a note and a position in its text string, and
 * overlays it by a possible sketch if sketch has a true value
 */
void
print_letter(int note, int letter, int sketch)
{
  int a = 2+6*(letter%10), b = 2+10*(letter/10);


  if (sketch) {
    XFillRectangle(display, app, fillGC, a, b, 6, 10);
#ifdef CREASES
    render_edit_wear_area(note, a, b, 6, 10);
#endif
    draw_sketch_area(note, a, b, 6, 10);
  }
  XDrawString(display, app, fontGC, a, b + font->ascent,
    &ndata[note].text[letter], 1);
}

/*
 * prints a note's entire text *without* drawing a possibly existing sketch
 */
void
print_text(int note)
{
  int i;

  for (i = 0; i < 59; i++) print_letter(note, i, 0);
}

/*
 * shifts part of a note's text to the right, inserting <ch> (step >= 1)
 */
void
shift_string(int note, int at, int margin, int step, int ch)
{
  int i;

  if (step < 1) return;

  for (i = margin; i >= at+step; i--) {
    ndata[note].text[i] = ndata[note].text[i-step];
    print_letter(note, i, 1);
  }
  for (i = at; i < at+step; i++) {
    ndata[note].text[i] = ch;
    print_letter(note, i, 1);
  }
}

/*
 * draws a rectangular section of a sketch, according to the data saved along
 * with a note
 */
void
draw_sketch_area(int note, int x, int y, int w, int h)
{
  int i, j;

  for (i = x; i < x+w; i++)
    for (j = y; j < y+h; j++)
      if (ndata[note].sketch[8*j + i/8] & (1<<(i%8)))
        XDrawPoint(display, app, fontGC, i, j);
}

/*
 * returns the number of the button bar's button corresponding to the given
:* coordinates, or -1
 */
int
bbar_button(int x, int y)
{
  int i;

  for (i = 0; i < 8; i++)
    if (x >= 4 + (i%4)*14 && x < 18 + (i%4)*14 && y >= 32 + (i/4)*14 &&
      y < 46 + (i/4)*14)
    {
      return i;
    }
  return -1;
}

/*
 * returns the number of that area on the alarm panel that corresponds to the
 * given coordinates, or -1
 */
int
abar_area(int x, int y)
{
  int i;

  /* relativate positions */
  x -= 3;
  y -= 4;
  for (i = 0; i < 6; i++) {
    coords_t c = abar_areas[i];
    if (x >= c.x && x < (c.x + (i < 4 ? 12 : 8)) &&
        y >= c.y && y < (c.y + (i < 4 ?  9 : 8)))
    {
      return i;
    }
  }
  return -1;
}

/*
 * displays note <note> in edit mode view
 */
void
render_note(int note)
{
  int i;

  XSetForeground(display, fillGC, palette[ndata[note].col].bg);
  XSetForeground(display, fontGC, BlackPixel(display, DefaultScreen(display)));
  XSetBackground(display, fontGC, palette[ndata[note].col].bg);
  set_mask(0);
  XFillRectangle(display, app, fillGC, 0, 0, 64, 64);
  XDrawRectangle(display, app, fontGC, 0, 0, 63, 63);
  XSetForeground(display, fontGC, palette[ndata[note].col].fg);
  for (i = 55; i < 62; i += 2)  /* draw triangle in the bottom right corner */
    XDrawLine(display, app, fontGC, i, 62, 62, i);
}

/*
 * prepares edit mode for note <note> (does NOT update the display)
 */
void
init_edit_mode(int note)
{
#ifdef CREASES
  XGCValues gcv;

  gcv.foreground = palette[ndata[note].col].cr;
  XChangeGC(display, creaseGC, GCForeground, &gcv);
#endif
  render_note(note);
#ifdef CREASES
  render_edit_wear(note);
#endif
  print_text(note);
  draw_sketch(note);
}

/*
 * updates a double digit area on the alarm panel
 */
inline void
render_abar_number(int field)
{
  int a, b;

  /* split two-digit number into separate digits */
  b = state.a_edit[field] % 10;
  a = (state.a_edit[field] / 10) % 10;
  /* copy appropriate digits */
  XCopyArea(display, digits, abar, normalGC, 6*a, 0, 6, 9,
    abar_areas[field].x, abar_areas[field].y);
  XCopyArea(display, digits, abar, normalGC, 6*b, 0, 6, 9,
    abar_areas[field].x+7, abar_areas[field].y);
}

/*
 * updates the alarm panel's switches for a given note
 */
inline void
render_abar_switches(int note)
{
  XPutImage(display, abar, normalGC, img,
    (ndata[note].a_flags & ALARM_DATE) ? 78 : 70, 55, 47, 4, 8, 8);
  XPutImage(display, abar, normalGC, img, 
    (ndata[note].a_flags & ALARM_DATE) ? 70 : 78, 55, 47, 16, 8, 8);
}

/*
 * renders the alarm panel for a given note
 */
void
render_abar(int note)
{
  int i;

  for (i = 0; i < 4; i++) render_abar_number(i);
  render_abar_switches(note);
}

/*
 * converts <note>'s contents to what's referred to as "cooked" format (as
 * opposed to raw data); returns a malloc()ed string that has to be freed
 * later on!
 */
char*
cook(int note, int pos, int len)
{
  char *s, *t;
  int i;

  s = smalloc(70);
  for (t = s, i = pos; i < (pos+len > 59 ? 59 : pos+len); i++) {
    if (!string_empty(&ndata[note].text[i], (i >= 50 ? 59 : 10*(i/10+1))-i))
    {
      if (t > s && !(i%10) && t[-1] != ' ' && t[-1] != '-')
        *t++ = ' ';  /* replace virtual newlines by spaces */
      if ((t > s && (ndata[note].text[i] != ' ' || t[-1] != ' ') &&
        !(t[-1] == '-'
        && string_empty(&ndata[note].text[10*(i/10)], i%10+1))) ||
        (t == s && ndata[note].text[i] != ' '))
      {
        *t++ = ndata[note].text[i];
      }
    }
  }
  *t = '\0';
  return s;
}

/*
 * dumps all notes' contents on STDOUT (raw dump unless <cooked> is true)
 */
void
dump_notes(int cooked)
{
  char *s;
  int c = 0, i, alarms;

  /* determine if any notes have alarms configured */
  time_next_alarm();
  alarms = state.alarm.note >= 0;

  /* print existing notes */
  for (; c < 4; c++)
    for (i = 0; i < notes_count; i++)
      if (c_group[ndata[i].col] == c) {
        s = cooked ? cook(i, 0, 59) : ndata[i].text;
        if (alarms) {  /* any alarms set at all? */
          if (ndata[i].a_flags & ALARM_ON) {
            explode_time(i);
            if (ndata[i].a_flags & ALARM_DATE) {  /* date-specific alarm */
              printf("%02d-%02d-%02d %02d:%02d #%02d: %s\n",
                state.a_edit[4]%100, state.a_edit[2], state.a_edit[3],
                state.a_edit[0], state.a_edit[1], i, s);
            } else {  /* daily alarm */
              printf("daily at %02d:%02d #%02d: %s\n", state.a_edit[0],
                state.a_edit[1], i, s);
            }
          } else  /* no alarm set for this note */
            printf("%14s #%02d: %s\n", "", i, s);
        } else  /* no alarms set at all */
          printf("#%02d: %s\n", i, s);
        if (cooked) free(s);
      }
}

/*
 * returns the (serial) number of the character (<x>, <y>) belongs to on a
 * note; prohibiting char #59 if <strict>
 */
int
char_at(int x, int y, int strict)
{
  int i;

  if (x < 2) x = 2;
  if (x > 62) x = 62;  /* intentional! */
  if (y > 61) y = 61;
  if (y < 2) y = 2;
  i = (x-2)/6 + ((y-2)/10)*10;
  if (i > 59) i = 59;
  return i;
}

#ifdef CREASES
/*
 * applies random wear & tear to a note (the default probability is multiplied
 * by <factor>)
 */
void
wear_note(int note)
{
  /* prefabricated crease patterns */
  static const int pats = 8;
  static const unsigned char pat[8][5][5] = {
    { { 1,0,0,0,0 },
      { 0,1,0,0,0 },
      { 0,0,1,0,0 },
      { 0,0,0,1,0 },
      { 0,0,0,0,1 } },
    { { 0,0,0,0,1 },
      { 0,0,0,1,0 },
      { 0,0,1,0,0 },
      { 0,1,0,0,0 },
      { 1,0,0,0,0 } },
    { { 0,0,0,1,0 },
      { 0,0,0,1,0 },
      { 0,0,1,0,0 },
      { 1,1,0,0,0 },
      { 0,0,0,0,0 } },
    { { 0,1,0,0,0 },
      { 0,1,0,0,0 },
      { 0,0,1,0,0 },
      { 0,0,0,1,1 },
      { 0,0,0,0,0 } },
    { { 0,0,0,0,0 },
      { 0,0,0,1,1 },
      { 0,0,1,0,0 },
      { 0,1,0,0,0 },
      { 0,1,0,0,0 } },
    { { 0,0,0,0,0 },
      { 1,1,0,0,0 },
      { 0,0,1,0,0 },
      { 0,0,0,1,0 },
      { 0,0,0,1,0 } },
    { { 0,0,0,0,0 },
      { 0,1,0,0,0 },
      { 0,0,1,0,0 },
      { 0,0,0,1,0 },
      { 0,0,0,0,0 } },
    { { 0,0,0,0,0 },
      { 0,0,0,1,0 },
      { 0,0,1,0,0 },
      { 0,1,0,0,0 },
      { 0,0,0,0,0 } }
  };
  int i, j, k = 0;

  /* determine number of crease bits */
  for (i = 0; i < 32; i++)
   for (j = 0; j < 8; j++)
     k += ndata[note].creases[i]>>(j) & 1;
  /* don't cover more than 35% of a note's area with creases */
  if (100*k/256 < 35 && !(rand() % (4+k/64))) {
    int x, y;
    int t = rand() % pats;  /* choose a prefab crease pattern to apply... */
    int a = rand() % 11;    /* ...as well as its location */
    int b = rand() % 11;

    for (y = 0; y < 5; y++)
      for (x = 0; x < 5; x++)
        if (pat[t][y][x])
          ndata[note].creases[2*(b+y) + (a+x<8)] |= 1<<(7-(a+x)%8);
  }
}

/*
 * makes an otherwise pre-rendered note look worn out
 */
void
render_wear(int note)
{
  int x, y, z;

  for (z = 0; z <= 48; z += 16)
    for (y = 0; y < 16; y++)
      for (x = 0; x < 16; x++)
        if (ndata[note].creases[2*y+x/8]>>(7-x%8) & 1 &&
          XGetPixel(img, 64+x, z+y) == palette[ndata[note].col].bg)
        {
          XPutPixel(img, 64+x, z+y, palette[ndata[note].col].cr);
        }
}

/*
 * renders wear & tear in edit mode in a specific window
 */
void
render_edit_wear_area_win(Window win, int note, int a, int b, int w, int h)
{
#define m(x, y) (m[2*(y) + (x)/8]>>(7-(x)%8) & 1)
#define l(x, y) (l[8*(y) + (x)/8]>>(7-(x)%8) & 1)
  static unsigned char l[(64/8)*64];  /* high res crease map */
  static unsigned char m[32];         /* low res crease map */
  int x, y, z;

  /* lazily render a high-res crease pattern */
  if (memcmp(m, ndata[note].creases, sizeof(m))) {
    memcpy(m, ndata[note].creases, sizeof(m));
    memset(l, 0, sizeof(l));
    /* now we have to somehow extrapolate a 64x64 crease pattern from a 16x16
       sketch... */
    for (z = 0; z < 3; z++)  /* three passes */
      for (y = 2; y < 62; y++)
        for (x = 2; x < 62; x++) {
          int sx = x/4, sy = y/4;
          int xx = x%4, yy = y%4;
          int xi = (xx<<1)%3, yi = (yy<<1)%3;
          int xd = xx<2 ? -1 : 1, yd = yy<2 ? -1 : 1;

          if (z < 2 && (  /* during passes #0 and #1 */
            (!z && xi && yi && m(sx, sy) &&
              (m(sx+xd, sy+yd) || m(sx, sy+yd) || m(sx+xd, sy) ) &&
              ((xd<0 && sx<2) || (xd>0 && sx>14) || (yd<0 && sy<2) ||
               (yd>0 && sy>14) || m(sx+2*xd, sy+2*yd))
            ) || (z && (
              (!xi && !yi && l(x-xd, y-yd) && l(x+2*xd, y+2*yd)) ||
              (!xi && yi && l(x-xd, y) && l(x+2*xd, y)) ||
              (xi && !yi && l(x, y-yd) && l(x, y+2*yd))
            ))))
          {
            l[8*y + x/8] |= 1<<(7-x%8);
          }
          /* during pass #2, remove isolated dots */
          if (z == 2 && l(x, y) && !(l(x-1, y-1) || l(x, y-1) ||
            l(x+1, y-1) || l(x-1, y) || l(x+1, y) || l(x-1, y+1) ||
            l(x, y+1) || l(x+1, y+1)))
          {
            l[8*y + x/8] &= 0xff - (1<<(7-x%8));
          }
        }
  }
  for (y = b; y < b+h; y++)
    for (x = a; x < a+w; x++)
      if (x > 1 && y > 1 && x < 62 && y < 62 && x+y <= 116 && l(x, y))
        XDrawPoint(display, win, creaseGC, x, y);
#undef m
#undef l
}
#endif

#ifdef FUNSTUFF

/*
 * atek sparppoirta ecaitnoo  npsceai lcoacisno,sr terusnt ur efit ah tahppnede
 */
void
check_occasion(int d, int m, int y)
{
  static const unsigned char sketch0[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0x3d, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xe7, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xaf, 0x4d, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xb3, 0x5d, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0x0d, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0x1f, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x78, 0x9f, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0xfe, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x60, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x60, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x3e, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
  };
  static const unsigned char sketch1[512] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x07, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0a, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0a, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0a, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x18, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x1b, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x66, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x48, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x6f, 0x61, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x97, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x90, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf8, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xc0, 0x0d, 0xc0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x80, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x60, 0x40, 0x80, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x80, 0x03, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x20, 0x08, 0x56, 0x06,
    0x00, 0x00, 0x00, 0x00, 0xe0, 0xf8, 0x1f, 0x06,
    0x00, 0x00, 0x00, 0x00, 0xf8, 0x6f, 0xfd, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x07, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
  };
  char *s;
  int note;

  if (!notes_count && !(state.state_bits & 1)) {  /* ewclmo een wsures */
    if ((note = add_note()) >= 0) {
      ndata[note].col = 15;
      memcpy(&ndata[note].sketch[256], sketch0, 256);
      s = csarbmel(
        "EWCLMO EotmwipbnaodrhTnaskf rortiygni  tuo!t               ");
      strcpy(ndata[note].text, s);
      free(s);
      ndata[note].cursor = 50;
      state.state_bits |= 1;
    }
  } else if (notes_count)
    state.state_bits |= 1;  /* purgdadeu essrw not'g tet ihs */

  if (m == 12 && d >= 25 && d <= 26) {  /* mXsa */
    if (!(state.state_bits & 2) && (note = add_note()) >= 0) {
      ndata[note].col = 14;
      memcpy(ndata[note].sketch, sketch1, 512);
      s = csarbmel(
        "          A M REYR    MXSA      T  O    Y UO !             ");
      strcpy(ndata[note].text, s);
      free(s);
      state.state_bits |= 2;
    }
  } else state.state_bits &= ~2;

  if (m == 1 && d <= 3) {  /* eN weYra */
    if (!(state.state_bits & 4) && (note = add_note()) >= 0) {
      ndata[note].col = 11;
      ndata[note].sketch[511] = 0x01;
      s = csarbmel(
        "nOeca agni aeyrah saapssde.. .A ynaw,y  A H PAYP  EN WEYRA!");
      strcpy(ndata[note].text, s);
      free(s);
      state.state_bits |= 4;
    }
  } else state.state_bits &= ~4;
}
#endif

/*
 * pastes <text> into note #<note>, starting at <offset>; returns the serial
 * number of the character right after the pasted string's end; overwrites
 * previous contents; tries to word-wrap unless <raw> has a true value
 */
int
paste_string(int note, int offset, const char *s, int raw)
{
  const char *t;
  int i, j;

  if (!raw) {  /* paste "cooked" */
    for (i = offset; *s && i < 59; ) {
      for (; *s && (*s == ' ' || *s == '\t' || *s == '\n'); s++);
      for (t = s; *t && (t == s || *t == '-' || *(t-1) != '-') &&
        *t != ' ' && *t != '\t' && *t != '\n'; t++);
      j = t-s;
      if (i < 50 && i%10 && i/10 != (i+j-1)/10) i = (i/10+1)*10;  /* next l.? */
      if (j && i <= 58) {
        if (i+j >= 58) {  /* word too long for note? */
          strncpy(&ndata[note].text[i], s, 59-i);
          i = 59;
        } else {
          strncpy(&ndata[note].text[i], s, j);
          i += j;
        }
        if (*(t-1) != '-' && i%10) i++;  /* insert blank? */
        s = t;
      }
    }
    if (ndata[note].text[i] == ' ')  /* place cursor right after the text */
      for (; i > offset && ndata[note].text[i-1] == ' '; i--);
    return i;
  } else {  /* paste raw */
    i = strlen(s);
    if (offset+i > 59) i = 59-offset;
    strncpy(&ndata[note].text[offset], s, i);
    return offset+i;
  }
}

/*
 * pastes <data> into note #<note> at offset <pos>, inserting if <ins>,
 * word-wrapping unless <raw>
 */
void
paste(int note, int pos, const char *data, int ins, int raw)
{
  char *s = 0;
  int i;

  if (ins) {
    if (raw) {
      s = smalloc(60);
      strcpy(s, ndata[note].text);
    } else {
      /* want to wrap subsequent text later, so we have to save it cooked */
      s = cook(note, 0, 59);
    }
    memset(&ndata[note].text[pos], ' ', 59-pos);
  } else
    memset(&ndata[note].text[pos], ' ', pos+strlen(data) > 59 ? 59-pos :
      strlen(data));
  i = paste_string(note, pos, data, raw);
  if (ins) {
    if (i <= 58) paste_string(note, i, &s[pos], raw);
    free(s);
  }
  ndata[note].cursor = i > 58 ? 58 : i;
}

/*
 * checks whether state.a_edit is valid (mday range w/respect to month...),
 * sets the hidden year field to a sensible value; the return value is boolean
 */
int
check_time()
{
  struct tm data, *ptr;
  time_t tt;

  tt = time(0);
  ptr = localtime(&tt);
  memcpy(&data, ptr, sizeof(struct tm));
  data.tm_isdst = -1;  /* let mktime() decide */
  data.tm_year = state.a_edit[2]-1 > ptr->tm_mon ||
    (state.a_edit[2]-1 == ptr->tm_mon && (state.a_edit[3] > ptr->tm_mday ||
    (state.a_edit[3] == ptr->tm_mday && (state.a_edit[0] > ptr->tm_hour ||
    (state.a_edit[0] == ptr->tm_hour && (state.a_edit[1] > ptr->tm_min ||
    (state.a_edit[1] == ptr->tm_min && !ptr->tm_sec))))))) ? ptr->tm_year :
    ptr->tm_year+1;
  data.tm_mon = state.a_edit[2]-1;
  data.tm_mday = state.a_edit[3];
  data.tm_hour = state.a_edit[0];
  data.tm_min = state.a_edit[1];
  /* check if date is convertible, i.e., valid */
  tt = mktime(&data);
  if (tt == -1) return 0;
  /* check if date has been adapted -> return failure */
  if (data.tm_mon != state.a_edit[2]-1 || data.tm_mday != state.a_edit[3])
    return 0;
  /* adapt date and return */
  state.a_edit[4] = (unsigned char) data.tm_year;  /* char suffices */
  return 1;
}

/*
 * transforms ndata[note].a_time to state.a_edit
 */
void
explode_time(int note)
{
  struct tm *ptr;

  if (ndata[note].a_time != -1) {
    ptr = localtime(&ndata[note].a_time);
    state.a_edit[0] = ptr->tm_hour;
    state.a_edit[1] = ptr->tm_min;
    state.a_edit[4] = ptr->tm_year;
  } else {  /* initialize on first call */
    time_t tt;
    time(&tt);
    tt += 60*60;
    ptr = localtime(&tt);
    state.a_edit[0] = ptr->tm_hour;
    state.a_edit[1] = 0;  /* minute */
    state.a_edit[4] = (unsigned char) ptr->tm_year;
  }
  state.a_edit[2] = ptr->tm_mon+1;
  state.a_edit[3] = ptr->tm_mday;
}

/*
 * transforms state.a_edit to ndata[note].a_time
 */
void
implode_time(int note)
{
  struct tm data, *ptr;
  time_t tt;

  time(&tt);
  ptr = localtime(&tt);
  memcpy(&data, ptr, sizeof(struct tm));
  data.tm_isdst = -1;  /* let mktime determine */
  data.tm_hour = state.a_edit[0];
  data.tm_min = state.a_edit[1];
  data.tm_sec = 0;
  /* the following 3 ain't required for daily notes, but this way, the date
     will be saved even in this case, so we set them */
  data.tm_year = state.a_edit[4];
  data.tm_mon = state.a_edit[2]-1;
  data.tm_mday = state.a_edit[3];
  ndata[note].a_time = mktime(&data);  /* shouldn't fail */
}

/*
 * evaluating its a_flags, returns an updated value of a note's a_time field,
 * considering <now> the current time if != -1
 */
time_t
adapt_time(int note, time_t now)
{
  time_t res = ndata[note].a_time;

  /* adaption is only necessary in the case of a daily alarm */
  if (!(ndata[note].a_flags & ALARM_DATE)) {
    struct tm atm, *ptr;

    if (now == -1) time(&now);
    ptr = localtime(&res);  /* break down alarm time */
    memcpy(&atm, ptr, sizeof(struct tm));
    ptr = localtime(&now);                 /* break down current time */
    atm.tm_year = ptr->tm_year;
    atm.tm_mon  = ptr->tm_mon;
    atm.tm_mday = ptr->tm_mday;
    atm.tm_sec  = 0;  /* just making sure */
    res = mktime(&atm);
    if (res < now) res += 24*60*60;  /* NOT `<='! */
  }

  return res;
}

/*
 * sets the state variable's alarm sub structure's values for the next alarm
 */
/*
    Together with appropriate calls from wmpinboard.c, this function results
    in the following strategy for scheduling alarms:

      - firstly, alarms with ALARM_DATE set are executed only once and get
        deactivated afterwards; daily alarms remain active even after an alarm
      - if the program is started after some alarm time was reach, the alarm
        is displayed if (and only if) it's a date-specific alarm
      - if an alarm is running while another one becomes due, this subsequent
        alarm will be run after the user has closed the other note
      - to achieve the latter, time_next_alarm() considers all notes with an
        alarm time equal to or greater than the previous one; if they're equal,
        the new alarm's note's ID must be greater than that of the previous
        alarm (this realizes a determined order for concurrent alarms and
        allows the program to avoid running one daily alarm several times in
        a row)
*/
void
time_next_alarm()
{
  time_t prev_time = state.alarm.time;
  int i, prev_note = state.alarm.note;

  if (state.alarm.note >= 0 && !state.alarm.run) prev_note = -1;
  if (prev_time == -1 || prev_time > time(0)) prev_time = time(0);
  state.alarm.time = -1;
  state.alarm.note = -1;
  state.alarm.run = 0;
  for (i = 0; i < notes_count; i++)
    if (ndata[i].a_flags & ALARM_ON) {
      time_t tt = adapt_time(i, prev_time);
      if (((ndata[i].a_flags & ALARM_DATE) || (tt == prev_time &&
        i > prev_note) || (tt > prev_time)) &&
        (state.alarm.time == -1 || tt < state.alarm.time))
      {
        state.alarm.time = tt;
        state.alarm.note = i;
      }
    }
  if (state.alarm.note < 0 && prev_note >= 0 &&
    (ndata[prev_note].a_flags & ALARM_ON))
  { /* if there is just one daily alarm, it hasn't been set again above, so
       let's do it here explicitly */
    state.alarm.note = prev_note;
    state.alarm.time = adapt_time(prev_note, prev_time+1);
  }
}

