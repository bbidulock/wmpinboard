/*
 *  Copyright (C) 1998-2000 by Marco G"otze.
 *
 *  This code is part of the wmpinboard source package, which is
 *  distributed under the terms of the GNU GPL2.
 */

#ifndef WMPINBOARD_H_INCLUDED
#define WMPINBOARD_H_INCLUDED

#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define MAX_NOTES 20  /* maximal number of notes */

#define STRING_BUF_SIZE 128  /* size of buffers for font descr. & suchlike */

#define C_NUM 20                   /* number of colors */
#define C_INNER palette[C_NUM].fg  /* mask color (interior of notes etc.) */
#define C_OUTER palette[C_NUM].bg  /* mask color (exterior of notes etc.) */
#define C_EXTRA palette[C_NUM].cr  /* additional mask color */

/* program modes with respect to interactive behavior... */
typedef enum {
  M_NOOP,  /* "normal" mode (pinboard view) */
  M_EDIT,  /* edit mode */
  M_MOVE,  /* note being dragged (implies M_NOOP) */
  M_BBAR,  /* button bar being displayed (implies M_EDIT) */
  M_DRAW,  /* sketch mode, drawing (implies M_EDIT) */
  M_ERAS,  /* sketch mode, erasing (implies M_EDIT) */
  M_ALRM   /* alarm active */
} modes;

typedef enum {
  /* command line actions */
  A_DUMP,       /* "cooked" dump */
  A_DUMP_RAW,   /* raw dump */
  A_IRON,       /* "iron" notes */
  A_DEL,        /* delete a now */
  A_ADD,        /* add "cooked" */
  A_ADD_RAW,    /* add raw */
  A_EXPORT,     /* export sketch */
  /* configuration options */
  C_FONT,       /* change font */
  C_THEME,      /* change board/panel pixmap */
  C_ALARM_CMD,  /* change alarm command */
  /* miscellani */
  M_INFO        /* prints miscellanous information */
} actions;

#define PANEL_ANI_INT  2000L  /* usecs interval for panel animation */
#define NOTE_ANI_INT  15000L  /* usecs interval for note animation */

#define DCLICK_LIMIT 500  /* ms */

#define FUNSTUFF

typedef enum {
  ALARM_ON   = 1<<0,
  ALARM_DATE = 1<<1
} alarm_flags_e;

typedef struct {
  int col;
  int x, y;
  char text[10*6];
  int cursor;
  char sketch[(64/8)*64];   /* bitfield; last byte used for other purposes... */
  char creases[(16/8)*16];  /* lower-res bitfield representing creases */
  time_t a_time;            /* alarm time */
  unsigned char a_flags;    /* alarm flags */
} data_t;

typedef struct {
  unsigned long fg, bg, cr;  /* foreground, background, crease color */
} palette_t;

typedef struct {  /* options and parameters */
  char *name;          /* the program's file name (argv[0]) */
  char *display;       /* alternate X display to connect to */
  int click_to_focus;  /* true if keyboard focus requires a click */
  int window_state;    /* NormalState, WithdrawnState? */
  int timeout;         /* timeout value in seconds */
  int animate;         /* use animations? */
  char font[STRING_BUF_SIZE];       /* font descriptor to remember */
  char theme[STRING_BUF_SIZE];      /* theme file to remember */
  char alarm_cmd[STRING_BUF_SIZE];  /* alarm command */
} opts_t;

typedef struct {  /* program state information */
  GC sketchGC;             /* temporary GC in sketch mode */
  XComposeStatus compose;  /* keyboard compose status */
  int clicks_count;        /* while emulating click-based focusing */
  int cur_note;            /* note currently being processed */
  int moved;               /* true if a note was *moved* (not just raised) */
  int button, dx, dy;      /* mouse-related stuff */
  modes mode;              /* program's current mode of operation */
  int bbar_pressed;        /* *pressed* panel button */
  int abar_pressed;        /* area on alarm panel that last received a click */
  int insert;              /* insert state in edit mode? */
  int selecting;           /* selection in progress? */
  int sel_from, sel_to;    /* used when selecting text via the mouse */
  int lp_btn;              /* button last pressed */
  Time lp_time;            /* time the last button was *pressed* */
  time_t idle;             /* for the timeout feature */
  volatile int alarmed;    /* used in animation timing */
  unsigned int state_bits; /* bit vector with special information */
  int raw_paste;           /* next paste to be raw? */
  int counter;             /* total number of notes ever created */
  struct {
    time_t time;           /* time_t of next alarm that's due */
    int note;              /* note that alarm.time is set for */
    int run;               /* true if the specified alarm was run */
    int phase;             /* phase of the animation */
    Pixmap buffer;         /* buffer for the animation phases */
  } alarm;
  unsigned char a_edit[5]; /* hour, minute, month, day, year being edited */
} state_t;

extern Display *display;
extern Window win;
extern XImage *img;
extern Pixmap app, bbar, abar, digits;
extern GC normalGC, fontGC, fillGC;
#ifdef CREASES
extern GC creaseGC;
#endif
extern XFontStruct *font;

extern const char c_group[C_NUM];
extern int notes_count;
extern data_t ndata[MAX_NOTES];
extern palette_t palette[C_NUM+1];
extern state_t state;

#endif  /* WMPINBOARD_H_INCLUDED */

