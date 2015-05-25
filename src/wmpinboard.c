/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#include <ctype.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>

#include "wmpinboard.h"
#include "misc.h"
#include "notes.h"
#include "xmisc.h"

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "../xpm/pinboard.xpm"
#include "../xpm/bbar.xpm"
#include "../xpm/abar.xpm"
#include "../xpm/digits.xpm"

const char *rc_file_name = "/.wmpinboarddata";  /* to be prepended with dir */

const char c_group[C_NUM] =  /* group available note colors */
  { 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 0, 0, 0 };

#define NUM_FONTS 3
const struct {  /* shortcuts for known fonts */
  char name[8];
  char desc[40];
  char font[60];
} fonts[NUM_FONTS] = {
  { "Default", "default font (ISO8859-1)", "-*-fixed-*--10-*-iso8859-1" },
  { "Latin2",  "Latin2 (ISO8859-2)",       "-*-fixed-*--10-*-iso8859-2" },
  { "Russian", "cyrillic font (KOI8-R)",   "-*-fixed-*--10-*-koi8-r" }
};
const char *default_font = fonts[0].font;

opts_t opts = { 0, 0, 0, -1, TIMEOUT, 1, "", "", "" };

palette_t palette[C_NUM+1];
Cursor cursors[3];  /* alternate X cursors */

int label_coords[4] = { 12, 0, 39, 9 };  /* default "TO DO" label coords */

data_t ndata[MAX_NOTES];  /* this holds all the notes */
int notes_count = 0;

state_t state = { 0, { 0, 0 }, 0, -1, -1, 0, 0, M_NOOP, -1, -1, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, { -1, -1, 0, 0, None } };

Display *display;
Window mainwin, iconwin, win;  /* win = visible window */
Pixmap app = None, bbar = None, abar = None, digits = None;
XImage *img = 0;
GC normalGC = 0, fontGC = 0, fillGC = 0;
#ifdef CREASES
GC creaseGC = 0;
#endif
XFontStruct *font = 0;

sigset_t sync_sig_set;  /* signals used for syncing with other instances */
int sync_sig_block_counter = 0;

                               /*************/
/******************************* FUNCTIONS ***********************************/
                             /*************/

void s_block();
void s_unblock();
int  notes_io(int);
void load_theme(const char*);
void init(void);
void done(void);
int  try_font(const char*, int);
void load_font(void);
void set_kbfocus(int);
void action(actions, const void*);
void help(void);
void parse_argv(int, char**);
void draw_cursor(int, int);
void set_cursor(int, int);
void sel_text(int);
void draw_pixel(int, int);
void set_mode(modes);
void quit_edit_mode(int, int);
void timer(unsigned int);
void animate_bbar(int);
void animate_abar(int);
void animate_note(int);
void animate_alarm();
void handle_ButtonPress(XEvent*);
void handle_ButtonRelease(XEvent*);
void handle_MotionNotify(XEvent*);
void handle_KeyPress(XEvent*);
void handle_sigs(int);

/*
 * blocks the signals in sync_sig_set
 */
inline void
s_block()
{
  if (!sync_sig_block_counter++)
    sigprocmask(SIG_BLOCK, &sync_sig_set, 0);
}

/*
 * inverse of the above
 */
inline void
s_unblock()
{
  if (!--sync_sig_block_counter)
    sigprocmask(SIG_UNBLOCK, &sync_sig_set, 0);
}

/*
 * reads or writes the data file; returns the PID the file was last written by
 *
 * file format revisions:
 *   0 = v0.7+: introduced binary data file format
 *   1 = v0.8+: added <data_t.{cursor,sketch}>
 *   2 = v0.9+: added <data_t.creases>, <state.state_bits>
 *   3 = v0.9.1+: added <PID>
 *   4 = v0.10+: added <theme>
 *   5 = v0.99: added <counter>
 *   6 = v0.99.1: added <alarm_cmd>, <data_t.a_{time,flags}>
 */
int
notes_io(int save)
{
  /* size of per-note records for specific revisions */
  static const int size_0 = sizeof(struct { int a, b, c; char d[60]; });
  static const int size_1 =
    sizeof(struct { int a, b, c; char d[60]; int e; char f[512]; });
  static const int size_2 =
    sizeof(struct { int a, b, c; char d[60]; int e; char f[512]; char g[32]; });
  static const int size_6 = sizeof(struct { int a, b, c; char d[60]; int e;
    char f[512]; char g[32]; time_t h; char i; });
  static const char *ext = ".new";
  static const char *header = "WMPB6";  /* data file header */
  char s[STRING_BUF_SIZE];
  char t[STRING_BUF_SIZE];
  FILE *file;
  int pid = (int) getpid();
  static int sizes[7];
  sizes[0] = size_0;
  sizes[1] = size_1;
  sizes[2] = size_2;
  sizes[3] = size_2;
  sizes[4] = size_2;
  sizes[5] = size_2;
  sizes[6] = size_6;

  s_block();

  s[sizeof(s)-1] = '\0';
  strncpy(s, getenv("HOME"), sizeof(s));
  if (sizeof(s)-strlen(s)-1 < strlen(rc_file_name)+strlen(ext))
    die("Buffer too small in notes_io().");
  strcat(s, rc_file_name);
  if (save) {  /* save to temporary file first, later rename it */
    strcpy(t, s);
    strcat(s, ext);
  }

  file = fopen(s, save ? "wb" : "rb");
  if (!file) {
    if (save)
      die("Couldn't open data file for writing.");
    else {
      s_unblock();
      return pid;  /* just don't read in anything if opening the file failed */
    }
  }

  if (save) {  /*** SAVE ***/
    fwrite(header, 5, 1, file);
    fputs(opts.font, file);
    fputc('\n', file);
    fputs(opts.theme, file);
    fputc('\n', file);
    fputs(opts.alarm_cmd, file);
    fputc('\n', file);
    fwrite(&pid, sizeof(pid), 1, file);
    fwrite(&state.state_bits, sizeof(state.state_bits), 1, file);
    fwrite(&state.counter, sizeof(state.counter), 1, file);
    fwrite(ndata, sizeof(data_t), notes_count, file);
    fclose(file);

    strcpy(s, t);
    strcat(s, ext);
    if (rename(s, t) == -1) {
      fprintf(stderr, "Fatal error: Failed to rename file `%s' to `%s'.\n", t,
        s);
      exit(EXIT_FAILURE);
    }
  } else {  /*** LOAD ***/
    int rev;
    void *dummy;
    int dummy2;

    (void) dummy;
    (void) dummy2;
    if (!fread(s, 5, 1, file) || strncmp(s, header, 4)) {  /* check header */
      fprintf(stderr, "Fatal error: Corrupt data file encountered.\n"
        "Delete `~%s' to start over.\n", rc_file_name);
      fclose(file);
      exit(EXIT_FAILURE);
    }
    /* file format revision check */
    s[5] = '\0';
    rev = strtol(&s[4], 0, 10);
    if (rev > 6) {
      fprintf(stderr, "Fatal error: The data file seems to have been created "
        "by a more recent version\nof wmpinboard.  Try and find a newer "
        "release, or remove `~%s'\nand lose all existing notes.\n",
        rc_file_name);
      fclose(file);
      exit(EXIT_FAILURE);
    }

    dummy = fgets(opts.font, sizeof(opts.font), file);
    if (strlen(opts.font) && opts.font[strlen(opts.font)-1] == '\n')
      opts.font[strlen(opts.font)-1] = '\0';

    if (rev >= 4) {
      dummy = fgets(opts.theme, sizeof(opts.theme), file);
      if (strlen(opts.theme) && opts.theme[strlen(opts.theme)-1] == '\n')
        opts.theme[strlen(opts.theme)-1] = '\0';
    }
    if (rev >= 6) {
      dummy = fgets(opts.alarm_cmd, sizeof(opts.alarm_cmd), file);
      if (strlen(opts.alarm_cmd) && opts.alarm_cmd[strlen(opts.alarm_cmd)-1] ==
        '\n')
      {
        opts.alarm_cmd[strlen(opts.alarm_cmd)-1] = '\0';
      }
    }
    if (rev >= 3) dummy2 = fread(&pid, sizeof(pid), 1, file);  /* last writer's PID */
    if (rev >= 2)  /* state.state_bits */
      dummy2 = fread(&state.state_bits, sizeof(state.state_bits), 1, file);
    else
      state.state_bits = 0;
    if (rev >= 5) /* counter */
      dummy2 = fread(&state.counter, sizeof(state.counter), 1, file);
    else
      state.counter = 0;

    notes_count = 0;
    while (notes_count < MAX_NOTES)
      if (fread(&ndata[notes_count], sizes[rev], 1, file)) {
        switch (rev) {
          case 0:
            ndata[notes_count].cursor = 0;
            memset(ndata[notes_count].sketch, 0, 512);
          case 1:
            memset(ndata[notes_count].creases, 0, 32);
          case 2:
          case 3:
          case 4:
          case 5:
            ndata[notes_count].a_time = -1;
            ndata[notes_count].a_flags = ALARM_DATE;
          default:
            notes_count++;
        }
      } else break;
    if (!state.counter) state.counter = notes_count;
    fclose(file);
    state.alarm.time = -1;
    state.alarm.note = -1;
    time_next_alarm();
  }

  s_unblock();
  return pid;
}

/*
 * loads theme from <filename>
 */
void
load_theme(const char *filename)
{
#ifdef LOW_COLOR
  if (strlen(filename))
    WARN("Skipped loading the configured theme since themes aren't\nsupported"
      "at low color depths.");
#else
  int coords[4], eof = 0, i;
  char s[STRING_BUF_SIZE], *t, *p, *q;
  struct stat buf;
  FILE *file;

  if (!strlen(filename)) return;
  file = fopen(filename, "r");
  if (!file) {
    WARN("Failed to open theme file; check whether the location you specified\n"
      "is valid.  Reverting to default.");
    return;
  }
  if (!fgets(s, sizeof(s), file) || strncmp(s, "WMPBtheme", 9)) {
    fclose(file);
    WARN("Configured theme file is corrupted!  Reverting to default.");
    return;
  }

  /* we'll need at most <file size> bytes to buffer the XPM data read from it */
  lstat(filename, &buf);
  if (!(t = malloc(buf.st_size))) {
    fclose(file);
    WARN("Skipped loading configured theme due to memory shortage.");
    return;
  }

  /* parse theme headers... */
  if (s[strlen(s)-1] == '\n') s[strlen(s)-1] = '\0';
  memset(s, ' ', 9);  /* kludge */
  do {
    if ((p = strchr(s, '#'))) *p = '\0';  /* # comments */
    p = s;
    while (*p) {
      for (; *p == ' ' || *p == '\t'; p++);
      for (q = p; isalpha(*q); q++);
      if (q > p) {
        if (!strncasecmp(p, "label", q-p)) {
          for (p = q; *p == ' ' || *p == '\t'; p++);
          if (*p == '=') {
            for (p++; *p == ' ' || *p == '\t'; p++);
            for (i = 0; *p && i < 4; i++) {
              coords[i] = (int) strtol(p, &p, 10);
              for (; *p && (*p < '0' || *p > '9'); p++);
            }
            q = p;
            if (i >= 4)
              if (
                coords[0] < 0 || coords[0] > 51 ||
                coords[2] < 0 || coords[2] > 51 ||
                coords[1] < 0 || coords[1] > 59 ||
                coords[3] < 0 || coords[3] > 59 ||
                coords[0] >= coords[2] || coords[1] >= coords[3])
              {
                WARN("Theme header `label' followed by coordinates outside the"
                  "\nallowed range--ignored.");
              } else {
                if ((coords[2]-coords[0])*(coords[3]-coords[1]) >= 16)
                  memcpy(&label_coords, &coords, sizeof(coords));
                else
                  WARN("Label Label area defined by the configured theme's "
                    "headers is too small--ignored.");
              }
            else
              WARN("Theme header `label' followed by improper specification--"
                "ignored.");
          } else {
            WARN("Theme header `label' lacks specification.");
            q = p;
          }
        } else
          fprintf(stderr, "Warning: Invalid header field in theme: `%s'.\n", p);
      }
      for (p = q; *p && *p != ' ' && *p != '\t'; p++);
    }
    eof = !fgets(s, sizeof(s), file);
    if (strlen(s) && s[strlen(s)-1] == '\n') s[strlen(s)-1] = '\0';
  } while (!eof && strlen(s));

  /* now read all pixmap data sections from the remainder of the file... */
  while (!eof) {
    Pixmap pic;
    XpmAttributes attr;

    attr.valuemask = XpmExactColors | XpmCloseness;
    attr.exactColors = False;
    attr.closeness = 65536;
    for (p = t; !(eof = !fgets(s, sizeof(s), file)) && strlen(s) && *s != '\n';)
    {
      strcpy(p, s);
      p += strlen(s);
    }
    if (!strstr(t, "char")) continue;  /* silently ignore garbage */
    if (XpmCreatePixmapFromBuffer(display, RootWindow(display,
      DefaultScreen(display)), t, &pic, 0, &attr) == XpmSuccess)
    {
      if (attr.width == 52 && attr.height == 60)  /* board pixmap */
        XGetSubImage(display, pic, 0, 0, 52, 60, ~0, ZPixmap, img, 6, 2);
      else if (attr.width == 58 && attr.height == 30)  /* bbar pixmap */
        XCopyArea(display, pic, bbar, normalGC, 0, 0, 58, 30, 0, 0);
      else if (attr.width == 58 && attr.height == 28)  /* abar pixmap */
        XCopyArea(display, pic, abar, normalGC, 0, 0, 58, 28, 0, 0);
      else if (attr.width == 60 && attr.height == 9)  /* abar digits */
        XCopyArea(display, pic, digits, normalGC, 0, 0, 60, 9, 0, 0);
      else /* invalid pixmap */
        WARN("Configured theme contains pixmap of invalid dimensions--"
          "ignored.");
      XFreePixmap(display, pic);
    } else
      WARN("Encountered invalid pixmap data in configured theme--ignored.");
  }
  free(t);
#endif
}

/*
 * tries to exit properly
 */
void
done(void)
{
  if (normalGC) XFreeGC(display, normalGC);
  if (fontGC) XFreeGC(display, fontGC);
  if (fillGC) XFreeGC(display, fillGC);
#ifdef CREASES
  if (creaseGC) XFreeGC(display, creaseGC);
#endif
  if (font) XFreeFont(display, font);
  if (app != None) XFreePixmap(display, app);
  if (bbar != None) XFreePixmap(display, bbar);
  if (abar != None) XFreePixmap(display, abar);
  if (digits != None) XFreePixmap(display, digits);
  if (state.alarm.buffer != None) XFreePixmap(display, state.alarm.buffer);
  if (img) XDestroyImage(img);
  XCloseDisplay(display);
  if (opts.display) free(opts.display);
}

/*
 * handler for caught signals
 */
void
handle_sigs(int sig)
{
  /* note: the kludges below will result in the application being terminated
     if the applet was destroyed via the respective WM option but not yet
     terminated (apparently WM just destroys the window in this case) */
  XWindowAttributes attr;
  const char *s = 0;

  s_block();

  switch (sig) {
    case SIGALRM:  /* used in animation timing */
      state.alarmed = 1;
      break;
    case SIGUSR1:
      XGetWindowAttributes(display, win, &attr);  /* kludge */
      /* quit edit mode w/saving; if not in edit mode, just save */
      if (state.mode == M_MOVE) set_mode(M_NOOP);
      if (state.mode != M_NOOP)
        quit_edit_mode(0, 1);  /* saves */
      else
        notes_io(1);
      break;
    case SIGUSR2:
      XGetWindowAttributes(display, win, &attr);  /* kludge */
      /* quit edit mode w/o saving... */
      if (state.mode != M_NOOP && state.mode != M_MOVE) quit_edit_mode(0, 0);
      notes_io(0);  /* reread data */
      notes_io(1);  /* rewrite data file */
      render_pinboard(-1);
      redraw_window();
      break;
    case SIGINT:
      s = "SIGINT";
    case SIGTERM:
      if (!s) s = "SIGTERM";
      fprintf(stderr, "Caught %s.  Trying to exit cleanly...\n", s);
      if (state.mode != M_NOOP) {
        if (note_empty(state.cur_note)) remove_note(state.cur_note);
        notes_io(1);
      }
      exit(EXIT_SUCCESS);
  }

  s_unblock();
}

/*
 * initializes the application's X window, installs a signal handler
 */
void
init(void)
{
  struct sigaction sigact;
  XWMHints wmhints;
  XSizeHints shints;
  Atom atom;
  XTextProperty name;
  XGCValues gcv;
  unsigned long gcm;
  int screen;

  /* set up syncsig_set */
  sigemptyset(&sync_sig_set);
  sigaddset(&sync_sig_set, SIGUSR1);
  sigaddset(&sync_sig_set, SIGUSR2);
  /* install signal handler */
  sigact.sa_handler = handle_sigs;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  if (sigaction(SIGALRM, &sigact, 0) < 0 ||
    sigaction(SIGUSR1, &sigact, 0) < 0 ||
    sigaction(SIGUSR2, &sigact, 0) < 0 ||
    sigaction(SIGINT, &sigact, 0) < 0 ||
    sigaction(SIGTERM, &sigact, 0) < 0)
  {
    die("Unable to install signal handlers.");
  }

  if (!(display = XOpenDisplay(opts.display))) {
    fprintf(stderr, "Fatal error: Can't open display `%s'.\n", 
      XDisplayName(opts.display));
    exit(EXIT_FAILURE);
  }

  /* try to set up done() to be called upon exit() */
  if (atexit(done)) die("Failed to set up exit procedure.");

  screen = DefaultScreen(display);

  atom = XInternAtom(display, "WM_DELETE_WINDOW", 0);
  if (atom == None) die("No window manager running.");

  /* try to autodetect whether we're running Window Maker... */
  if (opts.window_state < 0)  /* no window state explicitly specified */
    opts.window_state = XInternAtom(display, "_WINDOWMAKER_WM_FUNCTION", 1) !=
      None ? WithdrawnState : NormalState;

  mainwin = create_win();
  wmhints.window_group = mainwin;
  wmhints.initial_state = opts.window_state;
  wmhints.flags = StateHint | WindowGroupHint | IconWindowHint;
  if (opts.animate) wmhints.flags |= XUrgencyHint;  /* of any use at all? */
  if (opts.window_state == WithdrawnState) {
    iconwin = create_win();
    wmhints.icon_window = iconwin;
    win = iconwin;
  } else {
    wmhints.icon_window = None;
    win = mainwin;
  }
  XSetWMHints(display, mainwin, &wmhints);
  XSetWMProtocols(display, mainwin, &atom, 1);

  shints.min_width = 64;
  shints.min_height = 64;
  shints.max_width = 64;
  shints.max_height = 64;
  shints.x = 0;
  shints.y = 0;
  shints.flags = PMinSize | PMaxSize | USPosition;
  XSetWMNormalHints(display, mainwin, &shints);

  XSelectInput(display, win, ButtonPressMask | ExposureMask |
    ButtonReleaseMask | PointerMotionMask | StructureNotifyMask |
    KeyPressMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask);
  if (!XStringListToTextProperty(&opts.name, 1, &name))
    die("Can't allocate window name.");
  XSetWMName(display, mainwin, &name);

  set_mask(1);
  app = get_xpm((char**) pinboard_xpm);
  XMapSubwindows(display, win);

  gcm = GCForeground | GCBackground | GCGraphicsExposures;
  gcv.foreground = WhitePixel(display, screen);
  gcv.background = BlackPixel(display, screen);
  gcv.graphics_exposures = 0;
  normalGC = XCreateGC(display, RootWindow(display, screen), gcm, &gcv);
}

/*
 * tries to load font name, dies upon an error if <fatal> is true
 */
int
try_font(const char *name, int fatal)
{
  if (font) return 1;
  if (!name || !*name) return 0;
  font = XLoadQueryFont(display, name);
  if (font) {
    if (font->max_bounds.rbearing - font->min_bounds.lbearing > 7 ||
      font->max_bounds.ascent + font->max_bounds.descent > 10)
    {
      fprintf(stderr, "Warning: The font `%s'\n"
        "lacks a fixed character cell size of 6x10.\n", name);
      XFreeFont(display, font);
      font = 0;
    } else {
      if (!notes_count)
        strncpy(opts.font, name, sizeof(opts.font));
      return 1;
    }
  } else
    fprintf(stderr, "Warning: The font `%s' doesn't exist.\n", name);
  if (fatal)
    die("No alternatives left, aborting.");
  else
    fprintf(stderr, "Trying to revert...\n");
  return 0;
}

/*
 * loads the font to be used
 */
void
load_font(void)
{
  XGCValues gcv;
  unsigned long gcm;

  try_font(opts.font, 0);
  try_font(default_font, 1);

  gcm = GCForeground | GCBackground | GCFillStyle | GCLineStyle | GCFont |
    GCGraphicsExposures;
  gcv.foreground = C_OUTER;
  gcv.background = C_INNER;
  gcv.font = font->fid;
  gcv.fill_style = FillSolid;
  gcv.line_style = LineSolid;
  gcv.graphics_exposures = 0;
  fontGC = XCreateGC(display, app, gcm, &gcv);
#ifdef CREASES
  gcm ^= GCBackground | GCFont;
  creaseGC = XCreateGC(display, app, gcm, &gcv);
#endif
}

/*
 * (un)sets the keyboard input focus to wmpinboard's window
 */  
void 
set_kbfocus(int get_it)
{
  if (get_it)
    XSetInputFocus(display, win, RevertToPointerRoot, CurrentTime);
  else if (!opts.click_to_focus)
    XSetInputFocus(display, PointerRoot, RevertToNone, CurrentTime);
}

/*
 * takes an action as specified by <type> (for command-line options affection
 * notes rather than interactive behavior)
 */
void
action(actions type, const void *data)
{
  const char *s;
  int pid, running;
  int i, j, k;
  FILE *file;

  pid = notes_io(0);
  running = flush_instance(pid);
  if (running) pid = notes_io(0);
  switch (type) {
    case C_FONT:
      if (running) die("Can't change font while another instance is running.");
      if (notes_count) die("Can't change font if the pinboard isn't empty.");
      for (i = 0; i < NUM_FONTS; i++)
        if (!strcasecmp(data, fonts[i].name)) {
          strcpy(opts.font, fonts[i].font);
          break;
        }
      if (i >= NUM_FONTS) {  /* not a predefined font */
        if (strlen(data) >= sizeof(opts.font)) {
          /* avoid trouble when retrieving saved data... */
          fprintf(stderr, "Fatal error: Specified font descriptor exceeds "
            "buffer size of %lu bytes.\n", sizeof(opts.font));
          exit(EXIT_FAILURE);
        }
        strcpy(opts.font, data);
      }
      notes_io(1);
      fprintf(stderr, "Successfully changed the font.\n");
      break;
    case C_THEME:
      if (running) die("Can't change theme while another instance is running.");
      if (!strlen(data) || !strcasecmp(data, "default")) {
        opts.theme[0] = '\0';
        fprintf(stderr, "Reverted to default theme.\n");
      } else {
        if (strlen(data) >= sizeof(opts.theme)) {
          fprintf(stderr, "Fatal error: Specified theme file location exceeds "
            "buffer size of %lu bytes.\n", sizeof(opts.theme));
          exit(EXIT_FAILURE);
        }
        strcpy(opts.theme, data);
        if (!(file = fopen(opts.theme, "r")))
          WARN("Can't open specified theme file.");
        else
          fclose(file);
        fprintf(stderr, "Successfully configured for specified theme.\n");
      }
      notes_io(1);
      break;
    case C_ALARM_CMD:
      if (!strlen(data)) {
        opts.alarm_cmd[0] = '\0';
        fprintf(stderr, "Disabled alarm command.\n");
      } else {
        if (strlen(data) >= sizeof(opts.alarm_cmd)) {
          fprintf(stderr, "Fatal error: Specified theme file location exceeds "
            "buffer size of %lu bytes.\n", sizeof(opts.alarm_cmd));
          exit(EXIT_FAILURE);
        }
        strcpy(opts.alarm_cmd, data);
        fprintf(stderr, "Successfully configured specified alarm command.\n");
      }
      notes_io(1);
      if (running) {
        kill(pid, SIGUSR2);
        sleep(1);  /* don't return to the prompt too quickly... */
      }
      break;
    case A_DUMP:
      dump_notes(1);
      break;
    case A_DUMP_RAW:
      dump_notes(0);
      break;
#ifdef CREASES
    case A_IRON:
      for (i = 0; i < notes_count; memset(ndata[i++].creases, 0, 32));
      notes_io(1);
      if (running) {
        kill(pid, SIGUSR2);
        sleep(1);  /* don't return to the prompt too quickly... */
      }
      fprintf(stderr,
        "Hey, ironing isn't part of my job contract, you know...\n");
      break;
#endif
    case A_DEL:
      i = (int) strtol((const char*) data, 0, 10);
      if (i < 0 || i >= notes_count) die("Specified note doesn't exist.");
      remove_note(i);
      notes_io(1);
      if (running) { 
        kill(pid, SIGUSR2);
        sleep(1);  /* don't return to the prompt too quickly... */
      }
      fprintf(stderr, "Deleted note #%d.\n", i);
      break;
    case A_ADD: case A_ADD_RAW:
      if ((k = add_note()) < 0) die("Maximal number of notes reached.");
      s = (const char*) data;
      while (*s == '%') {
        if (!strncmp("%%", s, 2))
          s++;
        else if (strlen(s) >= 2 && *s == '%') {  /* color/position code given */
          if (isalpha(s[1])) {  /* color code? */
            i = 0;
            switch (toupper(s[1])) {
              case 'G':  i = 0; break;
              case 'Y':  i = 1; break;
              case 'R':  i = 2; break;
              case 'B':  i = 3; break;
              default: die("Unknown color code.");
            }
            while (c_group[ndata[k].col] != i) ndata[k].col = rand() % C_NUM;
          }
          if (!isalpha(s[1])) {  /* position code? */
            i = s[1] - '0';
            if (!i--) die("Invalid position code.");
            ndata[k].x = 6+i%3*12 + rand()%12;
            ndata[k].y = 3+(2-i/3)*14 + rand()%14;
          }
          s += 2;
        }
      }
      if (!strlen(s)) die("Won't add blank note.");
      i = paste_string(k, 0, s, type == A_ADD_RAW);
      ndata[k].cursor = i > 58 ? 58 : i;
      notes_io(1);
      if (running) { 
        kill(pid, SIGUSR2);
        sleep(1);  /* don't return to the prompt too quickly... */
      }
      fprintf(stderr, "Added note #%d.\n", k);
      break;
    case A_EXPORT:
      k = strtol((const char*) data, 0, 10);
      if (k < 0 || k > notes_count-1) die("Specified note doesn't exist.");
      puts("static const char sketch[512] = {");
      for (i = 0; i < 64; i++) {
        printf("  ");
        for (j = 0; j < 8; j++) {
          if (j) printf(", ");
          printf("0x%02x", (unsigned char) ndata[k].sketch[i*8+j]);
        }
        if (i < 63) printf(",");
        printf("\n");
      }
      puts("};");
      break;
    case M_INFO:
      for (i = 0, k = 0; i < notes_count; i++)
        if (ndata[i].a_flags & ALARM_ON) k++;
      printf("user configuration:\n"
        "  - font: %s\n"
        "  - theme: %s\n"
        "  - alarm command: %s\n",
        strlen(opts.font) ? opts.font : fonts[0].font,
        strlen(opts.theme) ? opts.theme : "[default]",
        strlen(opts.alarm_cmd) ? opts.alarm_cmd : "[none]");
      printf("\nuseless statistics:\n"
        "  - wmpinboard has saved you (at least) %d real note%s so far\n"
        "  - there %s currently %d note%s on your board\n"
        "  - %d note%s\n",
        state.counter, (state.counter != 1 ? "s" : ""),
        (notes_count != 1 ? "are" : "is"), notes_count,
        (notes_count != 1 ? "s" : ""), k,
        (k != 1 ? "s have alarms set" : " has an alarm set"));
  }
  exit(EXIT_SUCCESS);
}

/*
 * prints a help screen and exits
 */
void
help(void)
{
  int i;

  printf("wmpinboard v" VERSION "\n\n"
    "Copyright (C) 1998-2000 by Marco G\"otze, <mailto:gomar@mindless.com>.\n"
    "This program is distributed under the terms of the GNU GPL2.\n\n"
    "usage: %s [options]\n\n"
    "configuration directives (see the documentation for detailed information):\n"
    "           --font=FONT        use the specified font; FONT can be one of the\n"
    "                              following:\n",
    opts.name);
  for (i = 0; i < NUM_FONTS; i++)
    printf("                                %-8s  %s\n", fonts[i].name, fonts[i].desc);
  printf(
    "                              or a complete X descriptor of a fixed size 6x10\n"
    "                              font\n"
    "           --theme=FILE       use the specified theme rather than the default\n"
    "                              board/panel images (\"default\" or zero-length\n"
    "                              string reverts to default)\n"
    "           --alarm-cmd=CMD    execute the specified command on alarms\n"
    "                              (\"\" disables)\n\n"
    "run-time options:\n"
    "  -d DISP, --display=DISP     use the specified X display\n"
    "  -n,      --normal-state     force NormalState (AS Wharf)   \\ mutually\n"
    "  -w,      --withdrawn-state  force WithdrawnState (WM dock) / exclusive\n"
    "  -t TIME, --timeout=TIME     set edit mode timeout to TIME seconds\n"
    "                              (default %ds, 0 disables)\n"
    "  -c,      --click-to-focus   emulate click-based keyboard focus\n"
    "           --light            no animations\n\n"
    "command-line actions:\n"
    "           --dump             dump the contents of all notes\n"
    "           --dump-raw         dump the *raw* contents of all notes\n"
    "           --del=NUMBER       delete note NUMBER (as identified by a dump)\n"
    "           --add=STRING       add a note with STRING as its contents, trying\n"
    "                              to word-wrap the text\n"
    "           --add-raw=STRING   add a note with STRING as its *raw* contents\n\n"
    "general options:\n"
    "  -h,      --help             print this help\n"
    "  -i,      --info             print user configuration and statistical\n"
    "                              information\n"
    "  -v,      --version          print some more detailed version information\n\n"
    "See the wmpinboard(1) man page for more information, hints, and explanations.\n"
    "For themes and updates, check out the program's home page at\n"
    "<http://www.tu-ilmenau.de/~gomar/stuff/wmpinboard/>.\n",
    TIMEOUT);
  exit(EXIT_FAILURE);
}

/*
 * parses the list of command line arguments and initializes the application's
 * opts structure accordingly; handles non-interactive actions, and eventually
 * reads in the data file if the instance is to be run in interactive mode
 */
void
parse_argv(int argc, char **argv)
{
  static const struct option long_opts[] = {
    { "font", required_argument, 0, 'f' },
    { "theme", required_argument, 0, 'p' },
    { "alarm-cmd", required_argument, 0, 'r' },

    { "display", required_argument, 0, 'd' },
    { "normal-state", no_argument, 0, 'n' },
    { "withdrawn-state", no_argument, 0, 'w' },
    { "time", required_argument, 0, 't' },
    { "click-to-focus", no_argument, 0, 'c' },
    { "light", no_argument, 0, 'l' },

    { "dump", no_argument, 0, 'u' },
    { "dump-raw", no_argument, 0, 'y' },
    { "del", required_argument, 0, 'e' },
    { "add", required_argument, 0, 'a' },
    { "add-raw", required_argument, 0, 'b' },
#ifdef CREASES
    { "iron", no_argument, 0, 'z' },
#endif
    { "export-sketch", required_argument, 0, 'x' },

    { "help", no_argument, 0, 'h' },
    { "info", no_argument, 0, 'i' },
    { "version", no_argument, 0, 'v' },
    { 0, 0, 0, 0 }
  };
  static const char short_opts[] = "d:nwt:chiv";

  if (rindex(argv[0], '/'))
    opts.name = (char*) rindex(argv[0], '/') + 1;
  else
    opts.name = argv[0];

  for(;;) {
    int idx = 0, c;

    if ((c = getopt_long(argc, argv, short_opts, long_opts, &idx)) == -1)
      break;
    switch (c) {
      case 'f': action(C_FONT, optarg);
      case 'p': action(C_THEME, optarg);
      case 'r': action(C_ALARM_CMD, optarg);
      case 'd':  /* display */
        if (opts.display) free(opts.display);
        opts.display = smalloc(strlen(optarg)+1);
        strcpy(opts.display, optarg);
        break;
      case 'n':  /* NormalState */
        opts.window_state = NormalState;
        break;
      case 'w':  /* WithdrawnState */
        opts.window_state = WithdrawnState;
        break;
      case 't':  /* timeout */
        opts.timeout = strtol(optarg, 0, 10);
        if (opts.timeout < 0) opts.timeout = -opts.timeout;
        break;
      case 'c':  /* click-to-focus emulation */
        opts.click_to_focus = 1;
        break;
      case 'l':  /* light (no animations) */
        opts.animate = 0;
        break;
      case 'u': action(A_DUMP, 0);
      case 'y': action(A_DUMP_RAW, 0);
      case 'e': action(A_DEL, optarg);
      case 'a': action(A_ADD, optarg);
      case 'b': action(A_ADD_RAW, optarg);
#ifdef CREASES
      case 'z': action(A_IRON, 0);
#endif
      case 'x': action(A_EXPORT, optarg);
      case 'h': help();
      case 'i': action(M_INFO, 0);
      case 'v':
#if TIMEOUT == 0
#define PRINT1    "  - edit mode timeout is disabled by default\n"
#else
#define PRINT1    "  - default edit mode timeout is %d seconds\n"
#endif
#ifdef CREASES
#define PRINT2    "enabled\n"
#else
#define PRINT2    "disabled\n"
#endif
#ifndef FUNSTUFF
#define PRINT3    "  - FUNSTUFF is disabled  :-/\n"
#else
#define PRINT3
#endif
#if TIMEOUT != 0
#define PRINT4    , TIMEOUT
#endif
        printf("wmpinboard v" VERSION "\n\ncompile-time configuration:\n"
          "  - maximal number of notes is %d\n"
	  PRINT1
          "  - wear & tear of notes (creases) is "
          PRINT2
          PRINT3
          , MAX_NOTES
          PRINT4
          );
        exit(EXIT_SUCCESS);
      default : exit(EXIT_FAILURE);
    }
  }
  if (optind < argc) help();  /* exits */
  if (flush_instance(notes_io(0)))
    die("Another interactive instance is running.");
}

/*
 * draws a text cursor at character <pos> (block cursor if <block> is true)
 * _without_ updating the display
 */
void
draw_cursor(int pos, int block)
{
  unsigned long c = palette[ndata[state.cur_note].col].fg;
  unsigned long d = palette[ndata[state.cur_note].col].bg;
  int i, j;

  XGetSubImage(display, app, 2+6*(pos%10), 2+10*(pos/10), 6, 10, ~0, ZPixmap,
    img, 64, 54);
  for (i = 64; i < 70; i++)
    for (j = 63; j > (block ? 53 : 62); j--)
      XPutPixel(img, i, j, XGetPixel(img, i, j) == c ? d : c);
  XPutImage(display, app, normalGC, img, 64, 54, 2+6*(pos%10), 2+10*(pos/10),
    6, 10);
}

/*
 * in edit mode, moves the cursor to a new position and updates the display
 * if <update> has a true value
 */ 
void  
set_cursor(int new_pos, int update)
{
  int in_sel = 0;

  if (new_pos > 58) return;
  in_sel = (state.sel_from >= 0 && state.sel_to >= 0 &&
    ((state.sel_from <= new_pos && new_pos < state.sel_to) ||
    (state.sel_to <= new_pos && new_pos < state.sel_from)));
  if (!in_sel || state.insert)
    print_letter(state.cur_note, ndata[state.cur_note].cursor, 1);
  draw_cursor(ndata[state.cur_note].cursor = new_pos, !in_sel && state.insert);
  if (update) redraw_window();
}

/*
 * selects text in the character range state.sel_from..<to>, previously
 * unselecting that between state.sel_from..state.sel_to
 */
void
sel_text(int to)
{
  int i, t;
 
  if (to == state.sel_to) return;
  if (state.sel_to >= 0) {
    i = state.sel_from < state.sel_to ? state.sel_from : state.sel_to;
    t = state.sel_from < state.sel_to ? state.sel_to : state.sel_from;
    for (; i < t; i++) print_letter(state.cur_note, i, 1);
  }
  print_letter(state.cur_note, ndata[state.cur_note].cursor, 1);
  i = state.sel_from < to ? state.sel_from : to;
  t = state.sel_from < to ? to : state.sel_from;
  for (; i < t; i++) draw_cursor(i, 1);
  state.sel_to = to;
  set_cursor(ndata[state.cur_note].cursor, 1);
}

/*
 * clears the selection if in edit mode and text is selected
 */
void
clear_selection()
{
  if (state.mode == M_EDIT && state.sel_from >= 0) {
    state.sel_from = state.sel_to = -1;
    init_edit_mode(state.cur_note);
    set_cursor(ndata[state.cur_note].cursor, 1);
  }
}

/*
 * in sketch mode, draws a pixel at (x, y)
 */
void
draw_pixel(int x, int y)
{
  if (!x || !y || x > 62 || y > 62 || x+y >= 115) return;
  XDrawPoint(display, win, state.sketchGC, x, y);  /* actual drawing */
  if (state.mode == M_DRAW)  /* save bits */
    ndata[state.cur_note].sketch[x/8 + y*8] |= 1<<(x%8);
  else
    ndata[state.cur_note].sketch[x/8 + y*8] &= ~(1<<(x%8));
}

/*
 * sets the internal mode indicator and installs a corresponding mouse cursor
 */
void
set_mode(modes new)
{
  switch (state.mode = new) {
    case M_MOVE:
      XDefineCursor(display, win, cursors[state.cur_note >= 0 ? 0 : 1]);
      break;
    case M_DRAW: case M_ERAS:
      XDefineCursor(display, win, cursors[2]);
      break;
    case M_EDIT:
      state.selecting = 0;
      state.sel_from = state.sel_to = -1;
      if (!opts.click_to_focus) set_kbfocus(1);
    default:
      XUndefineCursor(display, win);
  }
}

/*
 * returns from M_EDIT, M_DRAW, M_ERAS, or M_BBAR to M_NOOP; destroys the
 * current note if empty or <destroy> is true
 */
void
quit_edit_mode(int destroy, int save)
{
  if (state.mode == M_BBAR) {
    if (ndata[state.cur_note].a_flags & ALARM_ON) animate_abar(0);
    animate_bbar(0);
  }
  if (destroy || (state.cur_note >= 0 && note_empty(state.cur_note))) {
    remove_note(state.cur_note);
    destroy = 1;
  }
  animate_note(destroy ? 6 : 5);
  set_mode(M_NOOP);
  if (save) notes_io(1);  /* should be last when called from signal handler */
  time_next_alarm();
}

/*
 * sets a timer expiring every <intv> microseconds
 */
void
timer(unsigned int intv)
{
#ifndef __GLIBC__
  struct itimerval val = { { 0, intv }, { 0, intv } };

  setitimer(ITIMER_REAL, &val, 0);
#else
  ualarm(intv, intv);
#endif
}

/*
 * adds some eyecandy to the popping-up of the button bar (slides in if <in>
 * is true, otherwise, out)
 */
void
animate_bbar(int in)
{
  int y;

  init_edit_mode(state.cur_note);
  set_cursor(ndata[state.cur_note].cursor, 0);
  if (in) {  /* slide in */
    if (opts.animate) {
      redraw_window();
      timer(PANEL_ANI_INT);
      for (y = 27; y >= 0; y -= 3) {
        state.alarmed = 0;
        XCopyArea(display, bbar, win, normalGC, 0, 0, 58, 30-y, 3, 34+y);
        flush_expose();
        while (!state.alarmed);
      }
      alarm(0);
      XCopyArea(display, bbar, win, normalGC, 0, 0, 58, 30, 3, 31);
      XCopyArea(display, app, win, normalGC, 3, 61, 58, 3, 3, 61);
      flush_expose();
      /* for future refreshes... */
      XCopyArea(display, bbar, app, normalGC, 0, 0, 58, 30, 3, 31);
    } else {  /* no animation */
      XCopyArea(display, bbar, app, normalGC, 0, 0, 58, 30, 3, 31);
      redraw_window();
    }
  } else {  /* slide out */
    if (opts.animate) {
      timer(PANEL_ANI_INT);
      for (y = 31; y <= 58; y += 3) {
        state.alarmed = 0;
        XCopyArea(display, app, win, normalGC, 3, y, 58, 3, 3, y);
        XCopyArea(display, bbar, win, normalGC, 0, 0, 58, 61-y, 3, y+3);
        flush_expose();
        while (!state.alarmed);
      }
      alarm(0);
      XCopyArea(display, app, win, normalGC, 3, 61, 58, 3, 3, 61);
      flush_expose();
    } else  /* no animation */
      redraw_window();
  }
}

/*
 * like animate_bbar, but for abar (to be called when bbar is already visible)
 */
void 
animate_abar(int in)
{
  int y;

  if (in) {  /* slide in */
    explode_time(state.cur_note);
    render_abar(state.cur_note);
    if (opts.animate) {
      redraw_window();
      timer(PANEL_ANI_INT);
      for (y = 28; y >= 3; y -= 3) {
        state.alarmed = 0;
        XCopyArea(display, abar, win, normalGC, 0, 0, 58, 31-y, 3, y);
        flush_expose();
        while (!state.alarmed);
      }
      alarm(0);
      XCopyArea(display, abar, win, normalGC, 0, 0, 58, 28, 3, 4);
      flush_expose();
      /* for future refreshes... */
      XCopyArea(display, abar, app, normalGC, 0, 0, 58, 28, 3, 4);
    } else {  /* no animation */
      XCopyArea(display, abar, app, normalGC, 0, 0, 58, 28, 3, 4);
      redraw_window();
    }
  } else {  /* slide out */
    implode_time(state.cur_note);
    /* render app as note plus bbar */
    init_edit_mode(state.cur_note);
    set_cursor(ndata[state.cur_note].cursor, 0);
    XCopyArea(display, win, app, normalGC, 3, 31, 58, 30, 3, 31);
    if (opts.animate) {
      timer(PANEL_ANI_INT);
      for (y = 4; y <= 25; y += 3) {
        state.alarmed = 0;
        XCopyArea(display, app, win, normalGC, 3, y, 58, 3, 3, y);
        XCopyArea(display, abar, win, normalGC, 0, 0, 58, 28-y, 3, y+3);
        flush_expose();
        while (!state.alarmed);
      }
      alarm(0);
      XCopyArea(display, app, win, normalGC, 3, 28, 58, 3, 3, 28);
      flush_expose();
    } else  /* no animation */
      redraw_window();
  }
}

/*
 * animates the switching between two notes (replaces what's currently
 * being displayed by state.cur_note), in a way specified by <style>
 *
 *   0 = right -> left   2 = bottom -> top   4 = pinboard -> edit mode
 *   1 = left -> right   3 = top -> bottom   5 = edit mode -> pinboard
 *                                           6 = note destruction
 */
void
animate_note(int style)
{
  static const int seq[11] = { 2, 3, 6, 9, 12, 12, 9, 6, 3, 2, 0 };
  XRectangle mask[5] = { { 6, 2, 52, 60 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 },
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
  int i, j;

  if (!opts.animate) {  /* animations disabled */
    if (style < 5) {  /* display note */
      set_mask(0);
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 1);
    } else {  /* display pinboard */
      set_mask(1);
      render_pinboard(-1);
      redraw_window();
    }
    return;
  }
  switch (style) {
    case 0:  /* slide right -> left */
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 0);
      timer(NOTE_ANI_INT);
      for (i = 2, j = 0; j < 10; i += seq[++j]) {
        state.alarmed = 0;
        XCopyArea(display, win, win, normalGC, seq[j], 0, 64-seq[j], 64, 0, 0);
        XCopyArea(display, app, win, normalGC, i-seq[j], 0, seq[j], 64,
          64-seq[j], 0);
        flush_expose();
        while (!state.alarmed);
      }
      break;
    case 1:  /* slide left -> right */
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 0);
      timer(NOTE_ANI_INT);
      for (i = 2, j = 0; j < 10; i += seq[++j]) {
        state.alarmed = 0;
        XCopyArea(display, win, win, normalGC, 0, 0, 64-seq[j], 64, seq[j], 0);
        XCopyArea(display, app, win, normalGC, 64-i, 0, seq[j], 64, 0, 0);
        flush_expose();
        while (!state.alarmed);
      }
      break;
    case 2:  /* slide top -> bottom */
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 0);
      timer(NOTE_ANI_INT);
      for (i = 2, j = 0; j < 10; i += seq[++j]) {
        state.alarmed = 0;
        XCopyArea(display, win, win, normalGC, 0, 0, 64, 64-seq[j], 0, seq[j]);
        XCopyArea(display, app, win, normalGC, 0, 64-i, 64, seq[j], 0, 0);
        flush_expose();
        while (!state.alarmed);
      }
      break;
    case 3:  /* slide bottom -> top */
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 0);
      timer(NOTE_ANI_INT);
      for (i = 2, j = 0; j < 10; i += seq[++j]) {
        state.alarmed = 0;
        XCopyArea(display, win, win, normalGC, 0, seq[j], 64, 64-seq[j], 0, 0);
        XCopyArea(display, app, win, normalGC, 0, i-seq[j], 64, seq[j], 0,
          64-seq[j]);
        flush_expose();
        while (!state.alarmed);
      }
      break;
    case 4:  /* pinboard view -> edit mode */
      mask[1].x = mask[1].y = 0;
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 0);
      timer(NOTE_ANI_INT);
      for (i = 2, j = 0; j < 10; i += seq[++j]) {
        state.alarmed = 0;
        mask[1].width = mask[1].height = i;
        XShapeCombineRectangles(display, win, ShapeBounding, 0, 0, mask, 2,
          ShapeSet, 0);
        XCopyArea(display, app, win, normalGC, 64-i, 64-i, i, i, 0, 0);
        flush_expose();
        while (!state.alarmed);
      }
      break;
    case 5:  /* edit mode -> pinboard view */
      render_pinboard(-1);
      timer(NOTE_ANI_INT);
      for (i = 2, j = 0; j < 10; i += seq[++j]) {
        state.alarmed = 0;
        mask[1].x = mask[1].y = i;
        mask[1].width = mask[1].height = 64-i;
        XCopyArea(display, win, win, normalGC, i-seq[j], i-seq[j], 64-i, 64-i,
          i, i);
        XCopyArea(display, app, win, normalGC, i-seq[j], i-seq[j], seq[j], 64,
          i-seq[j], i-seq[j]);
        XCopyArea(display, app, win, normalGC, i, i-seq[j], 64-i, seq[j],
          i, i-seq[j]);
        XShapeCombineRectangles(display, win, ShapeBounding, 0, 0, mask, 2,
          ShapeSet, 0);
        flush_expose();
        while (!state.alarmed);
      }
      break;
    case 6:  /* note destruction */
      render_pinboard(-1);
      timer((int) 1.5*NOTE_ANI_INT);
      /* mask[1].x = mask[1].y = mask[2].y = mask[3].x = 0; */
      for (i = 4; i <= 32; i += 4) {
        state.alarmed = 0;
        for (j = 1; j < 5; j++) mask[j].width = mask[j].height = 32-i;
        mask[2].x = mask[3].y = mask[4].x = mask[4].y = 32+i;
        XCopyArea(display, win, win, normalGC, 4, 4, 32-i, 32-i, 0, 0);
        XCopyArea(display, win, win, normalGC, 28+i, 4, 32-i, 32-i, 32+i, 0);
        XCopyArea(display, win, win, normalGC, 4, 28+i, 32-i, 32-i, 0, 32+i);
        XCopyArea(display, win, win, normalGC, 28+i , 28+i, 32-i, 32-i, 32+i,
          32+i);
        XCopyArea(display, app, win, normalGC, 32-i, 0, 2*i, 64, 32-i, 0);
        XCopyArea(display, app, win, normalGC, 0, 32-i, 64, 2*i, 0, 32-i);
        XShapeCombineRectangles(display, win, ShapeBounding, 0, 0, mask, 5,
          ShapeSet, 0);
        flush_expose();
        while (!state.alarmed);
      }
  }
  alarm(0);
}

/*
 * displays the next phase of the alarm animation (flashing)
 */
void
animate_alarm()
{
  XCopyArea(display, state.alarm.buffer, win, normalGC,
    state.alarm.phase*64, 0, 64, 64, 0, 0);
  flush_expose();
  state.alarm.phase = !state.alarm.phase;
}

/*
 * called from main event loop whenever a ButtonPress event occurs
 */
void
handle_ButtonPress(XEvent *event)
{
  int i;

  if (state.button) return;
  state.button = event->xbutton.button;
  switch (state.mode) {
    case M_NOOP:  /* drag a note? */
      state.cur_note = selected_note(event->xbutton.x, event->xbutton.y);
      if (state.cur_note >= 0) {  /* drag existing note */
        state.dx = event->xbutton.x - ndata[state.cur_note].x;
        state.dy = event->xbutton.y - ndata[state.cur_note].y;
        state.moved = 0;
        render_pinboard(state.cur_note);
        XCopyArea(display, app, app, normalGC,
          ndata[state.cur_note].x, ndata[state.cur_note].y, 16, 16, 64, 48);
        color_notes(state.cur_note);
        pin_note(state.cur_note);
        redraw_window();  /* necessary in case of a single raising click */
        state.mode = M_MOVE;  /* don't set drag cursor immediately */
      } else if (
        event->xbutton.x >= 6+label_coords[0] &&
        event->xbutton.x <= 6+label_coords[2] &&
        event->xbutton.y >= 2+label_coords[1] &&
        event->xbutton.y <= 2+label_coords[3] &&
        selected_note(event->xbutton.x, event->xbutton.y) < 0 &&
        notes_count < MAX_NOTES-1)
      {  /* possibly drag new note from "TO DO" label */
        state.moved = 0;
        set_mode(M_MOVE);
      }
      break;
    case M_EDIT:  /* claim focus in click-based focus emulation mode */
      if (!opts.click_to_focus || state.clicks_count) {
        if (state.button != 2) {
          if (state.lp_btn == state.button &&
            event->xbutton.time-state.lp_time < DCLICK_LIMIT)
          {
            state.sel_from = i = char_at(event->xbutton.x, event->xbutton.y, 0);
            for (; state.sel_from &&
              ndata[state.cur_note].text[state.sel_from-1] != ' ' &&
              (state.button != 1 || state.sel_from%10 > 0); state.sel_from--);
            for (; i < 59 && ndata[state.cur_note].text[i] != ' ' &&
              (state.button != 1 || state.sel_from/10 == (i+1)/10 || i%10);
              i++);
            sel_text(i);
            cb_copy(&ndata[state.cur_note].text[state.sel_from], i - 
              state.sel_from);
          } else {
            clear_selection();
            state.sel_from = char_at(event->xbutton.x, event->xbutton.y, 0);
            state.selecting = 1;
          }
        }
      }
      if (opts.click_to_focus && event->xbutton.x+event->xbutton.y < 115) {
        if (!state.clicks_count) set_kbfocus(1);
        state.clicks_count++;
      }
      break;
    case M_BBAR:  /* remember which button was *pressed* */
      state.bbar_pressed = state.abar_pressed = -1;
      /* button pressed on bbar? */
      i = bbar_button(event->xbutton.x, event->xbutton.y);
      if (i >= 0) {  /* bbar button */
        state.bbar_pressed = i;
      } else if (ndata[state.cur_note].a_flags & ALARM_ON) {  /* abar? */
        i = abar_area(event->xbutton.x, event->xbutton.y);
		if (i >= 0) state.abar_pressed = i;
      }
      break;
    case M_DRAW: case M_ERAS:  /* draw in either sketch mode */
      if (state.button == 1) {
        draw_pixel(event->xbutton.x, event->xbutton.y);
#ifdef CREASES
        if (state.mode == M_ERAS) {
          render_edit_wear_area_win(win, state.cur_note, event->xbutton.x,
            event->xbutton.y, 1, 1);
        }
#endif
      }
      break;
    default:  /* keep the compiler happy */
      break;
  }
  state.lp_btn = state.button;
  state.lp_time = event->xbutton.time;
}

/*
 * called from main event loop whenever a ButtonRelease event occurs
 */
void
handle_ButtonRelease(XEvent *event)
{
  XGCValues gcv;
  char *s;
  int i;

  if (event->xbutton.button != state.button) return;
  switch (state.mode) {
    case M_NOOP: case M_MOVE:  /* add new note or edit existing one? */
      if (!state.moved && state.button == 1) {  /* left-click */
        if (
          event->xbutton.x >= 6+label_coords[0] &&
          event->xbutton.x <= 6+label_coords[2] &&
          event->xbutton.y >= 2+label_coords[1] &&
          event->xbutton.y <= 2+label_coords[3] &&
          selected_note(event->xbutton.x, event->xbutton.y) < 0)
        { /* add new note */
          if ((state.cur_note = add_note()) >= 0) {
            animate_note(4);
            set_kbfocus(1);
            set_mode(M_EDIT);
          } else  /* beep if maximum number of notes reached */
            XBell(display, 0);
        } else if (state.cur_note >= 0 && state.cur_note ==
          selected_note(event->xbutton.x, event->xbutton.y))
        { /* open existing note in edit mode */
#ifdef CREASES
          wear_note(state.cur_note);
#endif
          animate_note(4);
          state.clicks_count = 0;
          set_mode(M_EDIT);
        }
      } else {  /* moved */
        if (state.cur_note >= 0) {
          if (note_empty(state.cur_note)) {  /* new note */
            animate_note(4);
            set_kbfocus(1);
            set_mode(M_EDIT);
          } else {  /* existing note dragged via either left or right MB */
#ifdef CREASES
            wear_note(state.cur_note);
#endif
            notes_io(1);
            if (state.button == 1) {  /* keep level */
              render_pinboard(-1);
              redraw_window();
            } else {  /* raise */
              state.cur_note = raise_note(state.cur_note);
#ifdef CREASES
              pin_note(state.cur_note);
              redraw_window();
#endif
            }
            time_next_alarm();  /* note IDs may have changed */
          }
        }
      }
      if (state.mode != M_EDIT) set_mode(M_NOOP);
      break;
    case M_EDIT: case M_DRAW: case M_ERAS:
      i = char_at(event->xbutton.x, event->xbutton.y, 0);
      if (event->xbutton.x + event->xbutton.y >= 115 &&
        (!state.selecting || state.sel_from == i))
      {  /* clicked triangle? */
        if (state.button > 1) {  /* open panel */
          animate_bbar(1);
          if (ndata[state.cur_note].a_flags & ALARM_ON) {
            animate_abar(1);
            check_time(state.cur_note);  /* adjusts hidden year field */
          }
          if (!opts.click_to_focus) set_kbfocus(0);
          set_mode(M_BBAR);
        } else {  /* end edit mode */
          if (state.mode == M_DRAW || state.mode == M_ERAS) {
            XFreeGC(display, state.sketchGC);  /* free temporary GC */
            if (M_ERAS) {  /* restore normal note display first */
              print_text(state.cur_note);
              draw_sketch(state.cur_note);
              set_cursor(ndata[state.cur_note].cursor, 1);
            }
            set_mode(M_EDIT);
          }
          quit_edit_mode(0, 1);
          if (!opts.click_to_focus) set_kbfocus(0);
        }
      } else {
        if (state.mode == M_EDIT) {
          if (!opts.click_to_focus || state.clicks_count > 1) {
            if (state.button == 2) {
              ndata[state.cur_note].cursor = char_at(event->xbutton.x,
                event->xbutton.y, 1);
              state.raw_paste = 0;
              cb_paste(state.cur_note, state.insert);
              init_edit_mode(state.cur_note);
              set_cursor(ndata[state.cur_note].cursor, 1);
            } else if (state.selecting && state.sel_from != i) {
              sel_text(i);
              if (state.sel_from > state.sel_to) {
                i = state.sel_from;
                state.sel_from = state.sel_to;
                state.sel_to = i;
              }
              if (state.button == 1) {  /* copy cooked */
                s = cook(state.cur_note, state.sel_from, state.sel_to -
                  state.sel_from);
                cb_copy(s, -1);
                free(s);
              } else {  /* copy raw */
                cb_copy(&ndata[state.cur_note].text[state.sel_from],
                  state.sel_to - state.sel_from);
              }
            } else
              set_cursor(char_at(event->xbutton.x, event->xbutton.y, 1), 1);
          }
        } else if (state.button > 1) {  /* revert from sketch to edit mode */
          XFreeGC(display, state.sketchGC);
          init_edit_mode(state.cur_note);
          set_cursor(ndata[state.cur_note].cursor, 1);
          set_mode(M_EDIT);
        }
      }
      state.selecting = 0;
      break;
    case M_BBAR:  /* actions in panel mode */
      if (state.bbar_pressed >= 0) {
        if (state.bbar_pressed == bbar_button(event->xbutton.x,
          event->xbutton.y))  /* clicked on panel? */
        {
          switch (state.bbar_pressed) {
            case 0:  /* open/close alarm panel */
              if (!(ndata[state.cur_note].a_flags & ALARM_ON)) {
                /* open alarm panel, turn alarm on */
                animate_abar(1);
                ndata[state.cur_note].a_flags |= ALARM_ON;
              } else {  /* close alarm panel, turn alarm off */
                animate_abar(0);
                ndata[state.cur_note].a_flags &= ~ALARM_ON;
              }
              break;
            case 4:  /* change note color */
              i = ndata[state.cur_note].col;
              if (state.button > 1) {  /* previous color */
                if (--ndata[state.cur_note].col < 0)
                  ndata[state.cur_note].col = C_NUM-1;
              } else  /* next color */
                if (++ndata[state.cur_note].col == C_NUM)
                  ndata[state.cur_note].col = 0;
              init_edit_mode(state.cur_note);
              set_cursor(ndata[state.cur_note].cursor, 0);
              XCopyArea(display, bbar, app, normalGC, 0, 0, 58, 30, 3, 31);
              if (ndata[state.cur_note].a_flags & ALARM_ON)
                XCopyArea(display, abar, app, normalGC, 0, 0, 58, 28, 3, 4);
              redraw_window();
              break;
            case 1: case 5:  /* enter draw/erase mode */
              gcv.foreground = state.bbar_pressed == 1 ?
                palette[ndata[state.cur_note].col].fg :
                palette[ndata[state.cur_note].col].bg;
              state.sketchGC = XCreateGC(display, app, GCForeground, &gcv);
              if (ndata[state.cur_note].a_flags & ALARM_ON) animate_abar(0);
              animate_bbar(0);
              render_note(state.cur_note);
#ifdef CREASES
              render_edit_wear(state.cur_note);
#endif
              if (state.bbar_pressed == 1) {  /* erase: hide text */
                print_text(state.cur_note);
                set_cursor(ndata[state.cur_note].cursor, 0);
              }
              draw_sketch(state.cur_note);
              redraw_window();
              set_mode(state.bbar_pressed == 1 ? M_DRAW : M_ERAS);
              break;
            case 2:  /* clear note's text */
              memset(ndata[state.cur_note].text, 32, 59);
#ifdef CREASES
              wear_note(state.cur_note);
              wear_note(state.cur_note);
#endif
              if (ndata[state.cur_note].a_flags & ALARM_ON) animate_abar(0);
              animate_bbar(0);
              init_edit_mode(state.cur_note);
              set_cursor(0, 1);
              set_mode(M_EDIT);
              break;
            case 6:  /* clear sketch */
              memset(ndata[state.cur_note].sketch, 0, 511);  /* not last byte */
#ifdef CREASES
              wear_note(state.cur_note);
              wear_note(state.cur_note);
#endif
              if (ndata[state.cur_note].a_flags & ALARM_ON) animate_abar(0);
              animate_bbar(0);
              init_edit_mode(state.cur_note);
              set_cursor(ndata[state.cur_note].cursor, 1);
              set_mode(M_EDIT);
              break;
            case 3:  /* remove note */
              quit_edit_mode(1, 1);
              if (!opts.click_to_focus) set_kbfocus(0);
              break;
            case 7:  /* close button bar */
              if (ndata[state.cur_note].a_flags & ALARM_ON) animate_abar(0);
              animate_bbar(0);
              set_mode(M_EDIT);
          }
        }
      } else if (state.abar_pressed >= 0) {
        if (state.abar_pressed == abar_area(event->xbutton.x,
          event->xbutton.y))  /* clicked on panel? */
        {
          if (state.abar_pressed < 4) {  /* clicked on number */
            char c = state.a_edit[state.abar_pressed];
            char delta;
            switch (state.button) {
              case 1:  delta = 1;   break;  /* Left mouse button */
              case 3:  delta = -1;  break;  /* Right mouse button */
              case 4:  delta = 1;   break;  /* Scrollwheel up */
              case 5:  delta = -1;  break;  /* Scrollwheel down */
              default: delta = 0;   break;  /* Unknown button; ignore */
            }

            switch (state.abar_pressed) {
              case 0:  c = (24+c+delta)%24;  break;
              case 1:  c = (60+c+delta)%60;  break;
              case 2:  c = (11+c+delta)%12+1;  break;
              case 3:  c = (30+c+delta)%31+1;
            }
            state.a_edit[state.abar_pressed] = c;
            /* check validity of date, adapt month or day if necessary */
            while (!check_time())
              if (state.abar_pressed == 2) {  /* month was changed */
                if (state.a_edit[3] > 1)
                  state.a_edit[3]--;
                else
                  break;  /* just making sure */
              } else {  /* presumably, the day was changed */
                state.a_edit[3] = 1;
                break;
              }
            /* update display */
            render_abar_number(state.abar_pressed);
            /* always update day in case the date had to be corrected */
            if (state.abar_pressed != 3) render_abar_number(3);
          } else {  /* clicked on switch */
            if (state.abar_pressed == 4)  /* daily alarm */
              ndata[state.cur_note].a_flags &= ~ALARM_DATE;
            else  /* specific date */
              ndata[state.cur_note].a_flags |= ALARM_DATE;
            check_time(state.cur_note);  /* updates the hidden year field */
            render_abar_switches(state.cur_note);
          }
          /* update display */
          XCopyArea(display, abar, app, normalGC, 0, 0, 58, 28, 3, 4);
          XCopyArea(display, abar, win, normalGC, 0, 0, 58, 28, 3, 4);
        }
      }
      break;
    case M_ALRM:
      if (ndata[state.cur_note].a_flags & ALARM_DATE)
        ndata[state.cur_note].a_flags &= ~ALARM_ON;
      notes_io(1);
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 1);
      redraw_window();
      set_mode(M_EDIT);
      time(&state.idle);
  }
  state.button = 0;
}

/*
 * called from main event loop whenever a MotionNotify event occurs
 */
void
handle_MotionNotify(XEvent *event)
{
  int i, j;

  switch (state.mode) {
    case M_MOVE:  /* note's being dragged, update position */
      if (state.cur_note >= 0) {  /* update note that's being moved's pos. */
        /* the drag cursor isn't set immediately... */
        if (!state.moved) {
          set_mode(M_MOVE);
          state.moved = 1;
        }
        i = ndata[state.cur_note].x;
        j = ndata[state.cur_note].y;
        ndata[state.cur_note].x = event->xbutton.x - state.dx;
        ndata[state.cur_note].x = ndata[state.cur_note].x < 6 ? 6 :
          ndata[state.cur_note].x;
        ndata[state.cur_note].x = ndata[state.cur_note].x > 42 ? 42 :
          ndata[state.cur_note].x;
        ndata[state.cur_note].y = event->xbutton.y - state.dy;
        ndata[state.cur_note].y = ndata[state.cur_note].y < 2 ? 2 :
          ndata[state.cur_note].y;
        ndata[state.cur_note].y = ndata[state.cur_note].y > 46 ? 46 :
          ndata[state.cur_note].y;
        XCopyArea(display, app, app, normalGC, 64, 48, 16, 16, i, j);
        XCopyArea(display, app, app, normalGC, ndata[state.cur_note].x,
          ndata[state.cur_note].y, 16, 16, 64, 48);
        pin_note(state.cur_note);
        redraw_window();
        time(&state.idle);
      } else {  /* create note by dragging it "off" the "TO DO" label */
        state.cur_note = add_note();
        ndata[state.cur_note].x = event->xbutton.x-8;
        ndata[state.cur_note].y = event->xbutton.y < 8 ? 0 :
          event->xbutton.y-8;
        state.dx = 8;
        state.dy = event->xbutton.y - ndata[state.cur_note].y;
        state.moved = 0;
        XCopyArea(display, app, app, normalGC,
          ndata[state.cur_note].x, ndata[state.cur_note].y, 16, 16, 64, 48);
        color_notes(state.cur_note);
        set_mode(M_MOVE);  /* update cursor */
        pin_note(state.cur_note);
        redraw_window();
        time(&state.idle);
      }
      break;
    case M_DRAW: case M_ERAS:  /* draw in either sketch mode */
      if (state.button == 1) {
        draw_pixel(event->xbutton.x, event->xbutton.y);
        if (state.mode == M_ERAS) {  /* enlarge the cursor a bit when erasing */
          draw_pixel(event->xbutton.x-1, event->xbutton.y);
          draw_pixel(event->xbutton.x+1, event->xbutton.y);
          draw_pixel(event->xbutton.x, event->xbutton.y-1);
          draw_pixel(event->xbutton.x, event->xbutton.y+1);
#ifdef CREASES
          render_edit_wear_area_win(win, state.cur_note, event->xbutton.x-1,
            event->xbutton.y, 3, 1);
          render_edit_wear_area_win(win, state.cur_note, event->xbutton.x,
            event->xbutton.y-1, 1, 3);
#endif
        }
        time(&state.idle);
      }
      break;
    case M_EDIT:
      if (state.selecting) {
        sel_text(char_at(event->xbutton.x, event->xbutton.y, 0));
        time(&state.idle);
      }
      break;
    default:  /* keep the compilter happy */
      break;
  }
}

/*
 * called from main event loop whenever a KeyPress event occurs
 */
void
handle_KeyPress(XEvent *event)
{
  KeySym ksym;
  char ch[4];
  char *s;
  int i, j = 0;

  if (state.mode != M_EDIT || event->xkey.state & 0xdfe8 ||
    (InputContext && XFilterEvent(event, win)))
  {
    XSendEvent(display, RootWindow(display, DefaultScreen(display)), True,
      KeyPressMask, event);
    if (state.mode == M_EDIT) set_kbfocus(1);  /* make sure we keep focus */
    return;
  }
  time(&state.idle);

  clear_selection();
  if (event->xkey.state & ControlMask) { /* [Ctrl]-anything -> special fn.s */
    /* InputContext intentionally ignored here... */
    XLookupString(&event->xkey, (char*) ch, sizeof(ch), &ksym, &state.compose);
    switch (ksym) {
      case XK_c: case XK_C:  /* copy cooked */
        state.sel_from = 0;
        sel_text(59);
        s = cook(state.cur_note, 0, 59);
        cb_copy(s, -1);
        free(s);
        return;
      case XK_r: case XK_R:  /* copy raw */
        state.sel_from = 0;
        sel_text(59);
        cb_copy(ndata[state.cur_note].text, -1);
        return;
      case XK_i: case XK_I:  /* paste raw */
        state.raw_paste = 1;
        cb_paste(state.cur_note, state.insert);
        init_edit_mode(state.cur_note);
        set_cursor(ndata[state.cur_note].cursor, 1);
        return;
      case XK_y: case XK_Y: case XK_z: case XK_Z:  /* zap line */
        for (i = 10*(ndata[state.cur_note].cursor/10); i < 59; i++)
          ndata[state.cur_note].text[i] =
            i < 49 ? ndata[state.cur_note].text[i+10] : ' ';
        init_edit_mode(state.cur_note);
        set_cursor(10*(ndata[state.cur_note].cursor/10), 1);
        return;
      case XK_n: case XK_N:  /* EMACS-style down */
        ksym = XK_Down;
        break;
      case XK_p: case XK_P:  /* EMACS-style up */
        ksym = XK_Up;
        break;
      case XK_f: case XK_F:  /* EMACS-style right */
        ksym = XK_Right;
        break;
      case XK_b: case XK_B:  /* EMACS-style left */
        ksym = XK_Left;
        break;
      default:
        return;
    }
  } else {
    if (InputContext) {
      Status status_return;
      j = XmbLookupString(InputContext, &event->xkey, ch, sizeof(ch),
        &ksym, &status_return);
    } else {
      j = XLookupString(&event->xkey, (char*) ch, sizeof(ch), &ksym,
        &state.compose);
    }
    if (!j && (ksym & 0xff00) == 0x0600) {  /* cyrillic keysyms */
      ch[0] = (unsigned char) (ksym & 0x00ff);
      ch[1] = '\0';
      if (ch[0] >= 0xbf || ch[0] == 0xa3 || ch[0] == 0xb3)
        j = 1;  /* filter extended KOI8 characters */
    }
  }
  switch (ksym) {
    case XK_Return: case XK_KP_Enter:
      if (ndata[state.cur_note].cursor >= 50) break;
      if (state.insert) {
        shift_string(state.cur_note, 10*(ndata[state.cur_note].cursor/10+1), 58,
          ndata[state.cur_note].cursor%10, ' ');
        shift_string(state.cur_note, ndata[state.cur_note].cursor, 58,
          10-ndata[state.cur_note].cursor%10, ' ');
      }
      set_cursor(10*(ndata[state.cur_note].cursor/10+1), 1);
      break;
    case XK_BackSpace: case 0xfd1a:
      if (!ndata[state.cur_note].cursor) break;
      if (ndata[state.cur_note].cursor == 58 &&
        ndata[state.cur_note].text[58] != ' ')
      { /* special behavior when on very last character */
        ndata[state.cur_note].text[58] = ' ';
        set_cursor(ndata[state.cur_note].cursor, 1);
        break;
      }
      print_letter(state.cur_note, ndata[state.cur_note].cursor--, 1);
    case XK_Delete: case XK_KP_Delete:
      j = 1;  /* delete entire line if empty... */
      if (((ksym != XK_BackSpace && ksym != 0xfd1a) ||
        ndata[state.cur_note].cursor%10 == 9) &&
        string_empty(&ndata[state.cur_note].text[10*
          (ndata[state.cur_note].cursor/10)], 10))
      {
        ndata[state.cur_note].cursor = 10*(ndata[state.cur_note].cursor/10);
        j = 10;
      }
      if (ksym == XK_BackSpace || ksym == 0xfd1a) {
        if (ndata[state.cur_note].cursor%10 == 9 &&
          ndata[state.cur_note].text[ndata[state.cur_note].cursor] == ' ')
        {
          for (; ndata[state.cur_note].text[ndata[state.cur_note].cursor-1] ==
            ' ' && ndata[state.cur_note].cursor >
            10*(ndata[state.cur_note].cursor/10);)
          {
            ndata[state.cur_note].text[ndata[state.cur_note].cursor--] = ' ';
          }
        }
      }
        
      for (i = ndata[state.cur_note].cursor; i < (j == 1 &&
        ndata[state.cur_note].cursor/10 < 5 ?
        10*(ndata[state.cur_note].cursor/10+1)-j : 59-j); i++)
      {
        ndata[state.cur_note].text[i] = ndata[state.cur_note].text[i+j];
        ndata[state.cur_note].text[i+j] = ' ';
        print_letter(state.cur_note, i, 1);
      }
      if (ndata[state.cur_note].cursor%10 == 9 ||
        ndata[state.cur_note].cursor == 58)
      { /* exceptions to the loop */
        ndata[state.cur_note].text[ndata[state.cur_note].cursor] = ' ';
      }
      if (j > 1)  /* only when lines have been shifted */
        init_edit_mode(state.cur_note);
      else
        print_letter(state.cur_note, i, 1);
      set_cursor(ndata[state.cur_note].cursor, 1);
      break;
    case XK_Up: case XK_KP_Up:
      if ((event->xkey.state & 0x01) == 0x01) {  /* next note w/similar color */
        for (i = state.cur_note == notes_count-1 ? 0 : state.cur_note+1;
           i != state.cur_note;)
        {
           if (c_group[ndata[i].col] == c_group[ndata[state.cur_note].col])
             break;
           if (++i >= notes_count) i = 0;
        }
        if (i == state.cur_note) break;
        if (note_empty(state.cur_note)) remove_note(state.cur_note);
        notes_io(1);
        state.cur_note = i;
        animate_note(2);
      } else  /* move ndata[state.cur_note].cursor */
        if (ndata[state.cur_note].cursor > 9)
          set_cursor(ndata[state.cur_note].cursor-10, 1);
      break;
    case XK_Down: case XK_KP_Down:
      if ((event->xkey.state & 0x01) == 0x01) {  /* prev. note w/simil. color */
        for (i = state.cur_note ? state.cur_note-1 : notes_count-1;
          i != state.cur_note;)
        {
           if (c_group[ndata[i].col] == c_group[ndata[state.cur_note].col])
             break;
           if (--i < 0) i = notes_count-1;
        }
        if (i == state.cur_note) break;
        if (note_empty(state.cur_note)) remove_note(state.cur_note);
        notes_io(1);
        state.cur_note = i;
        animate_note(3);
      } else  /* move ndata[state.cur_note].cursor */
        if (ndata[state.cur_note].cursor < 49)
          set_cursor(ndata[state.cur_note].cursor+10, 1);
      break;
    case XK_Left: case XK_KP_Left:
      if ((event->xkey.state & 0x01) == 0x01) {  /* previous note */
        if (notes_count == 1) break;
        if (note_empty(state.cur_note)) remove_note(state.cur_note);
        notes_io(1);
        if (--state.cur_note < 0) state.cur_note = notes_count-1;
        animate_note(1);
      } else  /* move cursor */
        if (ndata[state.cur_note].cursor)
          set_cursor(ndata[state.cur_note].cursor-1, 1);
      break;
    case XK_Right: case XK_KP_Right:
      if ((event->xkey.state & 0x01) == 0x01) {  /* next note */
        if (notes_count == 1) break;
        if (note_empty(state.cur_note)) {
          remove_note(state.cur_note);
          state.cur_note = notes_count-1;
        }
        notes_io(1);
        if (++state.cur_note == notes_count) state.cur_note = 0;
        animate_note(0);
      } else  /* move cursor */
        if (ndata[state.cur_note].cursor < 58)
          set_cursor(ndata[state.cur_note].cursor+1, 1);
      break;
    case XK_Tab: case XK_ISO_Left_Tab: /* change color */
      i = ndata[state.cur_note].col;
      if ((ksym == XK_Tab && (event->xkey.state & 0x01) == 0x01) ||
        ksym == XK_ISO_Left_Tab)
      {  /* previous color */
        if (--ndata[state.cur_note].col < 0)
          ndata[state.cur_note].col = C_NUM-1;
      } else  if (++ndata[state.cur_note].col == C_NUM)
        ndata[state.cur_note].col = 0;
      init_edit_mode(state.cur_note);
      set_cursor(ndata[state.cur_note].cursor, 0);
      redraw_window();
      break;
    case XK_Home: case XK_KP_Home:
      set_cursor(10*(ndata[state.cur_note].cursor/10), 1);
      break;
    case XK_End: case XK_KP_End:
      i = j = ndata[state.cur_note].cursor < 50 ?
        10*(ndata[state.cur_note].cursor/10)+9 : 58;
      if (ndata[state.cur_note].text[j] == ' ')
        for (; i > 10*(j/10) && ndata[state.cur_note].text[i-1] == ' '; i--);
      set_cursor(i, 1);
      break;
    case XK_Prior: case XK_KP_Prior:  /* page up */
      set_cursor(0, 1);
      break;
    case XK_Next: case XK_KP_Next:  /* page down */
      set_cursor(58, 1);
      break;
    case XK_Insert: case XK_KP_Insert:
      state.insert = !state.insert;
      set_cursor(ndata[state.cur_note].cursor, 1);
      break;
    case XK_Escape:
      quit_edit_mode(0, 1);
      if (!opts.click_to_focus) set_kbfocus(0);
      break;
    default:
      if (j) {
        if (state.insert)
          shift_string(state.cur_note, ndata[state.cur_note].cursor,
            ndata[state.cur_note].cursor < 50 ?
            10*(ndata[state.cur_note].cursor/10)+9 : 58, 1, ch[0]);
        else {
          ndata[state.cur_note].text[ndata[state.cur_note].cursor] = ch[0];
          print_letter(state.cur_note, ndata[state.cur_note].cursor, 1);
        }
        set_cursor(ndata[state.cur_note].cursor < 58 ?
          ndata[state.cur_note].cursor+1 : ndata[state.cur_note].cursor, 1);
      }
  }
}
                                   /********/
/*********************************** MAIN ************************************/
                                 /********/
int
main(int argc, char **argv)
{
#ifdef FUNSTUFF
  time_t tt = 0, t;
  struct tm ltt, lt;
#endif
  XEvent event;
  XGCValues gcv;
  int i, j;

  srand(time(0));
  umask(077);  /* users' notes needn't be world-readable */

  parse_argv(argc, argv);  /* evaluate command line parameters */
  init();                  /* initialize X window */
  XSetCommand(display, mainwin, argv, argc);
  XMapWindow(display, mainwin);
/*  init_xlocale();          * initialize input context */

  /* initialize internal images, palette, cursors */
  bbar = get_xpm((char**) bbar_xpm);
  abar = get_xpm((char**) abar_xpm);
  digits = get_xpm((char**) digits_xpm);
  img = XGetImage(display, app, 0, 0, 86, 64, ~0, ZPixmap);
  XGetSubImage(display, abar, 47, 4, 8, 8, ~0, ZPixmap, img, 70, 55);
  XGetSubImage(display, abar, 47, 16, 8, 8, ~0, ZPixmap, img, 78, 55);
  for (i = 0; i < C_NUM; i++) {
    palette[i].bg = XGetPixel(img, 80, i);
    palette[i].fg = XGetPixel(img, 81, i);
    palette[i].cr = XGetPixel(img, 82, i);
  }
  palette[C_NUM].fg = XGetPixel(img, 80, 63);  /* C_INNER */
  palette[C_NUM].bg = XGetPixel(img, 81, 63);  /* C_OUTER */
  palette[C_NUM].cr = XGetPixel(img, 82, 63);  /* C_EXTRA */
  gcv.foreground = C_OUTER;  /* dummy value */
  gcv.graphics_exposures = 0;
  fillGC = XCreateGC(display, app, GCForeground | GCGraphicsExposures, &gcv);
  cursors[0] = XCreateFontCursor(display, XC_fleur);  /* when moving a note */
  cursors[1] = XCreateFontCursor(display, XC_dotbox);  /* drag-create */
  cursors[2] = XCreateFontCursor(display, XC_pencil);  /* for sketch mode */

  load_font();
  load_theme(opts.theme);
  notes_io(1);  /* saves PID */
  render_pinboard(-1);
  redraw_window();
  set_mode(M_NOOP);

  for(;;) {  /*** MAIN EVENT LOOP ***/
    while (XPending(display)) {
      XNextEvent(display, &event);

      s_block();  /* BEGIN OF SYNC-PROHIBITING SECTION */

      switch (event.type) {
        case Expose:
          redraw_window();
          break;
        case ButtonPress:
          handle_ButtonPress(&event);
          time(&state.idle);
          break;
        case ButtonRelease:
          if (event.xbutton.button == state.button)
            handle_ButtonRelease(&event);
          time(&state.idle);
          break;
        case MotionNotify:
          handle_MotionNotify(&event);
          break;
        case EnterNotify:
          if (state.mode == M_EDIT && !opts.click_to_focus) set_kbfocus(1);
          state.clicks_count = 0;
          break;
        case LeaveNotify:
          if (state.mode == M_EDIT && !opts.click_to_focus) set_kbfocus(0);
          break;
        case KeyPress:
          handle_KeyPress(&event);
          break;
        case SelectionClear:
          clear_selection();
          cb_clear();
          break;
        case SelectionNotify:
          if (state.mode == M_EDIT) {
            cb_paste_external(event.xselection.requestor,
              event.xselection.property, 1, state.cur_note, state.insert,
              state.raw_paste);
            init_edit_mode(state.cur_note);
            set_cursor(ndata[state.cur_note].cursor, 1);
          }
          break;
        case SelectionRequest:
          handle_SelectionRequest(&(event.xselectionrequest));
      }
    }
    if (opts.timeout && (state.mode == M_EDIT || state.mode == M_BBAR ||
      state.mode == M_DRAW || state.mode == M_ERAS) &&
      time(0)-state.idle > opts.timeout)
    {
      quit_edit_mode(0, 1);  /* no set_kbfocus(0) here */
    }
#ifdef FUNSTUFF
    if (state.mode == M_NOOP) {
      time(&t);
      if (t-tt > 10*60 && localtime_r(&tt, &ltt) && localtime_r(&t, &lt)) {
        if (ltt.tm_mday != lt.tm_mday || ltt.tm_mon != lt.tm_mon ||
          ltt.tm_year != lt.tm_year)
        {
          i = notes_count;
          j = state.state_bits;
          check_occasion(lt.tm_mday, 1+lt.tm_mon, 1900+lt.tm_year);
          if (i != notes_count || j != state.state_bits) {  /* anything done? */
            notes_io(1);
            render_pinboard(-1);
            redraw_window();
          }
        }
        tt = t;
      }
    }
#endif
    if (state.mode == M_ALRM)
      animate_alarm();
    else if (state.mode == M_NOOP && state.alarm.note >= 0 &&
      state.alarm.time <= time(0))
    {  /* alarm due */
      state.cur_note = state.alarm.note;
      state.alarm.run = 1;
      if (ndata[state.cur_note].a_flags & ALARM_DATE)
        ndata[state.cur_note].a_flags &= ~ALARM_ON;
      animate_note(4);
      set_mode(M_ALRM);
      if (strlen(opts.alarm_cmd)) {
        char buf[STRING_BUF_SIZE+2];
        strcpy(buf, opts.alarm_cmd);
        strcat(buf, " &");
        if (system(buf))
		exit(1);
      }
      prepare_alarm_anim();
      state.alarm.phase = 0;
      animate_alarm();
    }

    s_unblock();  /* END OF SYNC-PROHIBITING SECTION */

    /* sleep for a while... */
    switch (state.mode) {
      case M_NOOP: usleep(100000L); break;
      case M_ALRM: usleep(500000L); break;  /* animation timing, 0.5s */
      default:     usleep( 10000L);
    }
  }
}

/*
                            CONCEPTUAL NOTES

                                                      bbar =
                                  show panel        +---------+
 mainwin[, iconwin]        ,------------------------|::panel::| 30
      (each) =             |       app =            |:::::::::|
    +----------+           |  +----------+--+       +---------+
    |          |           `->|          |::|           58     \
    | visible  |    display   |   draw   |::| 48              dto. for abar
 64 |   area   | <=========== |  buffer  |::|
    |          |     64x64    |          +-++
    |          |          ,-->|         <->|| 16
    +----------+          |   +----------+-++
         64               |        64    16\3
                          |                 \ used when
                          |                 moving a note     server-side
  ........................|...............................................
                          |
              restore     |                                   client-side
            ,----------->-+
            |  board      |    draw
     img =  |   16        +-<--------[bitfield]
    +----------+-++  pin  |   sketch
    |::::::::::| |------>-+
    |:copy:of::| || notes |
 64 |:pristine:| ||       |
    |:pinboard:+++|  copy |
    |:::::::10{||---<--->-'
    +----------++-+ character/write it back
         64    6\16
                /\                                            :: = const
               /  \ used to overlay a
              / character with the cursor
             /
    buffer of (8+8)x8 @ 70,55 for alarm panel switches
*/

