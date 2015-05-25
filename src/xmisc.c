/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/stat.h>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xlocale.h>
#include <X11/Xmd.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>

#include "wmpinboard.h"
#include "misc.h"
#include "notes.h"
#include "xmisc.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

typedef CARD32 Atom32;

XIC InputContext = 0;
static char *rs_inputMethod = 0;
static char *rs_preeditType = 0;
static unsigned char *cb_buffer = 0;

/*
 * replaces <o_col> by <n_col> in an XImage ([no]_col are XGetPixel return
 * values!)
 */
void
replace_color(XImage *img, int x, int y, int w, int h, unsigned long o_col,
  unsigned long n_col)
{
  int i, j;

  for (i = x; i < x+w; i++)
    for (j = y; j < y+h; j++)
      if (XGetPixel(img, i, j) == o_col)
        XPutPixel(img, i, j, n_col);
}

/*
 * puts <dest> on app, copying pixels only where <src>'s color is !=
 * <mask_col>'s
 */
void
merge_masked(XImage *src, int sx, int sy, int dx, int dy, int w, int h,
  unsigned long mask_col)
{
  GC tempGC;
  XGCValues gcv;
  unsigned long p;
  int i, j;

  gcv.foreground = mask_col;  /* dummy value */
  tempGC = XCreateGC(display, app, GCForeground, &gcv);
  for (i = 0; i < w; i++)
    for (j = 0; j < h; j++) {
      p = XGetPixel(src, sx+i, sy+j);
      if (p != mask_col) {
        XSetForeground(display, tempGC, p);
        XDrawPoint(display, app, tempGC, dx+i, dy+j);
      }
    }
  XFreeGC(display, tempGC);
}

/*
 * converts a pixmap structure to a Pixmap
 */
void
get_xpm_with_mask(char *pixmap_bytes[], Pixmap *pic, Pixmap *mask)
{
  XpmAttributes attr;

  attr.valuemask = XpmExactColors | XpmCloseness;
  attr.exactColors = False;
  attr.closeness = 65536;
  if (XpmCreatePixmapFromData(display, RootWindow(display,
    DefaultScreen(display)), pixmap_bytes, pic, mask, &attr) != XpmSuccess)
  {
    die("Not enough free color cells.");
  }
}

/*
 * calls get_xpm_with_mask() but returns just the pixmap
 */
Pixmap
get_xpm(char *pixmap_bytes[])
{
  Pixmap pic, mask;
  get_xpm_with_mask(pixmap_bytes, &pic, &mask);
  return pic;
}

/*
 * flushes Expose events
 */
void
flush_expose(void)
{
  XEvent dummy;

  while (XCheckTypedWindowEvent(display, win, Expose, &dummy));
}

/*
 * redraws the application's window
 */
void
redraw_window(void)
{
  flush_expose();
  XCopyArea(display, app, win, normalGC, 0, 0, 64, 64, 0, 0);
}

/*
 * sets either the app's shape mask for normal operation or reverts to none
 * (edit mode)
 */
void
set_mask(int set)
{
  static XRectangle none = { 0, 0, 64, 64 };
  static XRectangle mask = { 6, 2, 52, 60 };

  if (set)
    XShapeCombineRectangles(display, win, ShapeBounding, 0, 0, &mask, 1,
      ShapeSet, 0);
  else
    XShapeCombineRectangles(display, win, ShapeBounding, 0, 0, &none, 1,
      ShapeSet, 0);
}

/*
 * sets up XIC-orientated keyboard handling
 * [stolen from the RXVT, minor adaptations]
 */
void
init_xlocale(void)
{
  char *p, *s, buf[32], tmp[1024];
  XIM xim = 0;
  XIMStyle input_style = 0;
  XIMStyles *xim_styles = 0;
  int found;

  InputContext = 0;
  setlocale(LC_ALL, "");

  if (!rs_inputMethod || !*rs_inputMethod) {
    if ((p = XSetLocaleModifiers("@im=none")) && *p)
      xim = XOpenIM(display, 0, 0, 0);
  } else {
    strcpy(tmp, rs_inputMethod);
    for (s = tmp; *s;) {
      char *end, *next_s;
      
      for (; *s && isspace(*s); s++);
      if (!*s) break;
      end = s;
      for (; *end && (*end != ','); end++);
      next_s = end--;
      for (; (end >= s) && isspace(*end); end--);
      *(end + 1) = '\0';
      
      if (*s) {
        strcpy(buf, "@im=");
        strcat(buf, s);
        if ((p = XSetLocaleModifiers(buf)) && *p
          && (xim = XOpenIM(display, 0, 0, 0)))
        {
          break;
        }
      }
      if (!*next_s) break;
      s = (next_s + 1);
    }
  }

  if (!xim && (p = XSetLocaleModifiers("")) && *p)
    xim = XOpenIM(display, 0, 0, 0);
    
  if (!xim) {
#ifdef DEBUG_IC
    fprintf(stderr, "Failed to open input method.\n");
#endif
    return;
  }

  if (XGetIMValues(xim, XNQueryInputStyle, &xim_styles, 0) || !xim_styles) {
    XCloseIM(xim);
#ifdef DEBUG_IC
    fprintf(stderr, "Input method doesn't support any style.\n");
#endif
    return;
  }

  strcpy(tmp, (rs_preeditType ? rs_preeditType : "Root"));
  for (found = 0, s = tmp; *s && !found;) {
    unsigned short i;
    char *end, *next_s;
  
    while (*s && isspace(*s)) s++;
    if (!*s) break;
    end = s;
    while (*end && (*end != ',')) end++;
    next_s = end--;
    while ((end >= s) && isspace(*end)) *end-- = 0;
  
    if (!strcmp(s, "OverTheSpot"))
      input_style = (XIMPreeditPosition | XIMStatusArea);
    else if (!strcmp(s, "OffTheSpot"))
      input_style = (XIMPreeditArea | XIMStatusArea);
    else if (!strcmp(s, "Root"))
      input_style = (XIMPreeditNothing | XIMStatusNothing);
  
    for (i = 0; i < xim_styles->count_styles; i++) {
      if (input_style == xim_styles->supported_styles[i]) {
        found = 1;
         break;
      }
    }
    s = next_s;
  }
  XFree(xim_styles);
    
  if (found == 0) {
    XCloseIM(xim);
#ifdef DEBUG_IC
    fprintf(stderr, "Input method doesn't support my preedit type.\n");
#endif
    return;
  }

  if (input_style != (XIMPreeditNothing | XIMStatusNothing)) {
    XCloseIM(xim);
#ifdef DEBUG_IC
    fprintf(stderr, "This program only supports the `Root' preedit type.\n");
#endif
    return;
  }

  InputContext = XCreateIC(xim, XNInputStyle, input_style,
    XNClientWindow, win, XNFocusWindow, win, 0);

  if (!InputContext) {
    XCloseIM(xim);
  }
}

/*
 * creates a dock-app-tailored window
 */
Window
create_win()
{
  Window win;
  XClassHint hint;
  XSetWindowAttributes attr;

  win = XCreateSimpleWindow(display,
    RootWindow(display, DefaultScreen(display)), 0, 0, 64, 64, 0, 0, 0);
  hint.res_name = (char*) "wmpinboard";
  hint.res_class = (char*) "WMPINBOARD";
  XSetClassHint(display, win, &hint);
  attr.background_pixmap = ParentRelative;
  XChangeWindowAttributes(display, win, CWBackPixmap, &attr);
  return win;
}

/*
 * copies <text> to the X clipboard
 */
void
cb_copy(const char *text, int len)
{
  int l;

  if (!text) return;

  if (cb_buffer) free(cb_buffer);
  l = len < 0 ? strlen(text) : len;
  cb_buffer = smalloc(l+1);
  strncpy(cb_buffer, text, l);
  cb_buffer[l] = 0;

  XSetSelectionOwner(display, XA_PRIMARY, win, CurrentTime);
  if (XGetSelectionOwner(display, XA_PRIMARY) != win)
    WARN("Failed to set XA_PRIMARY ownership.");
  XChangeProperty(display, DefaultRootWindow(display), XA_CUT_BUFFER0,
    XA_STRING, 8, PropModeReplace, cb_buffer, l);
}

/*
 * responds to a SelectionRequest event
 * [once again, thanks to the RXVT source]
 */
void
handle_SelectionRequest(XSelectionRequestEvent *rq)
{
  XEvent ev;
  Atom32 target_list[2];
  static Atom xa_targets = None;

  if (xa_targets == None) xa_targets = XInternAtom(display, "TARGETS", 0);

  ev.xselection.type = SelectionNotify;
  ev.xselection.property = None;
  ev.xselection.display = rq->display;
  ev.xselection.requestor = rq->requestor;
  ev.xselection.selection = rq->selection;
  ev.xselection.target = rq->target;
  ev.xselection.time = rq->time;

  if (rq->target == xa_targets) {
    target_list[0] = (Atom32) xa_targets;
    target_list[1] = (Atom32) XA_STRING;
    XChangeProperty(display, rq->requestor, rq->property, rq->target,
      8*sizeof(target_list[0]), PropModeReplace,
      (unsigned char*) target_list,
      sizeof(target_list)/sizeof(target_list[0]));
    ev.xselection.property = rq->property;
  } else if (rq->target == XA_STRING) {
    XChangeProperty(display, rq->requestor, rq->property, rq->target,
      8, PropModeReplace, cb_buffer, strlen((char*) cb_buffer));
      ev.xselection.property = rq->property;
  }
  XSendEvent(display, rq->requestor, 0, 0, &ev);
}

/*
 * handle's the user's request to paste text into a note
 */
void
cb_paste(int note, int ins)
{
  Atom prop;

  if (cb_buffer) {
    paste(note, ndata[note].cursor, (const char*) cb_buffer, ins,
      state.raw_paste);
  } else if (XGetSelectionOwner(display, XA_PRIMARY) == None) {
    cb_paste_external(DefaultRootWindow(display), XA_CUT_BUFFER0, 0, note,
      ins, state.raw_paste);
  } else {
    prop = XInternAtom(display, "VT_SELECTION", 0);
    XConvertSelection(display, XA_PRIMARY, XA_STRING, prop, win, CurrentTime);
  }
}

/*
 * pastes the current contents of the clipboard into <note> at <pos>, inserting
 * or overwriting depending on <ins>, trying to word-wrap unless <raw>; moves
 * the cursor
 */
void
cb_paste_external(Window window, unsigned prop, int Delete, int note, int ins,
  int raw)
{
  unsigned long bytes_after, nitems;
  unsigned char *data;
  Atom actual_type;
  int actual_fmt;

  if (prop == None) return;
  if ((XGetWindowProperty(display, window, prop, 0, 64, Delete,
    AnyPropertyType, &actual_type, &actual_fmt, &nitems, &bytes_after,
    &data) != Success))
  {
    XFree(data);
    return;
  }
  if (nitems) paste(note, ndata[note].cursor, (const char*) data, ins, raw);
  XFree(data);
}

/*
 * frees the copy made of a selected string
 */
void
cb_clear()
{
  if (cb_buffer) {
    free(cb_buffer);
    cb_buffer = 0;
  }
}

/*
 * prepares the buffer which portions are copied from during an alarm animation
 */
void
prepare_alarm_anim()
{
  unsigned long wh = WhitePixel(display, DefaultScreen(display));
  unsigned long bl = BlackPixel(display, DefaultScreen(display));
  XImage *i;
  int x, y;

  if (state.alarm.buffer != None) XFreePixmap(display, state.alarm.buffer);
  /* create buffer pixmap */
  state.alarm.buffer = XCreatePixmap(display, win, 128, 64,
    DefaultDepth(display, DefaultScreen(display)));
  /* copy current edit view (note: strangely, if we copy from win and are
     running WM and this happens during WM start-up (so the dock isn't
     finished while executing this), wmpinboard gets terminated) */
  XCopyArea(display, app, state.alarm.buffer, normalGC, 0, 0, 64, 64, 0, 0);
  /* create inverted version */
  i = XGetImage(display, state.alarm.buffer, 0, 0, 64, 64, ~0, ZPixmap);
  for (y = 0; y < 64; y++)
    for (x = 0; x < 64; x++)
      XPutPixel(i, x, y,
        XGetPixel(i, x, y) == palette[ndata[state.cur_note].col].fg ? wh : bl);
  XPutImage(display, state.alarm.buffer, normalGC, i, 0, 0, 64, 0, 64, 64);
  XDestroyImage(i);
}

