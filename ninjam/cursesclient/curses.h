#ifndef _CURSES_WIN32SIM_H_
#define _CURSES_WIN32SIM_H_

#ifndef _WIN32
#include <curses.h>
#else

#include <windows.h>

#include "queue.h"


#ifndef CURSES_INSTANCE
#define CURSES_INSTANCE ____UNDEFINED_POO__
#endif

#define LINES ((CURSES_INSTANCE)->cursesCtx.lines)
#define COLS ((CURSES_INSTANCE)->cursesCtx. cols)

//ncurses WIN32 wrapper functions


#define addnstr(str,n) __addnstr(CURSES_INSTANCE,str,n)
#define addstr(str) __addnstr(CURSES_INSTANCE,str,-1)
#define addch(c) __addch(CURSES_INSTANCE,c)

#define mvaddstr(x,y,str) __mvaddnstr(CURSES_INSTANCE,x,y,str,-1)
#define mvaddnstr(x,y,str,n) __mvaddnstr(CURSES_INSTANCE,x,y,str,n)
#define clrtoeol() __clrtoeol(CURSES_INSTANCE)
#define move(x,y) __move(CURSES_INSTANCE, x,y, 0)
#define attrset(a) CURSES_INSTANCE->cursesCtx.m_cur_attr=(a)
#define bkgdset(a) CURSES_INSTANCE->cursesCtx.m_cur_erase_attr=(a)
#define initscr(flags) __initscr(CURSES_INSTANCE,flags)
#define endwin() __endwin(CURSES_INSTANCE)
#define curses_erase(x) __curses_erase(x)
#define start_color()
#define init_pair(x,y,z) __init_pair(&(CURSES_INSTANCE)->cursesCtx,x,y,z)
#define has_colors() 1



class jesusonicInstance;
void __addnstr(jesusonicInstance *inst, char *str,int n);
void __move(jesusonicInstance *inst, int x, int y, int noupdest);
static inline void __addch(jesusonicInstance *inst, char c) { __addnstr(inst,&c,1); }
static inline void __mvaddnstr(jesusonicInstance *inst, int x, int y, char *str, int n) { __move(inst,x,y,1); __addnstr(inst,str,n); }


void __clrtoeol(jesusonicInstance *inst);
void __initscr(jesusonicInstance *inst, int flags);
void __endwin(jesusonicInstance *inst);
void __curses_erase(jesusonicInstance *inst);

int curses_getch(jesusonicInstance *inst);

#define wrefresh(x)
#define cbreak()
#define noecho()
#define nonl()
#define intrflush(x,y)
#define keypad(x,y)
#define nodelay(x,y)
#define raw()
#define refresh()
#define sync()


#define A_NORMAL 0
#define A_BOLD 1
#define COLOR_PAIR(x) ((x)<<NUM_ATTRBITS)
#define COLOR_PAIRS 8
#define NUM_ATTRBITS 1

#define COLOR_WHITE RGB(192,192,192)
#define COLOR_BLACK RGB(0,0,0)
#define COLOR_BLUE  RGB(0,0,192)
#define COLOR_RED   RGB(192,0,0)
#define COLOR_CYAN  RGB(0,192,192)
#define COLOR_BLUE_DIM  RGB(0,0,56)
#define COLOR_RED_DIM    RGB(56,0,0)
#define COLOR_CYAN_DIM  RGB(0,56,56)

#define ERR -1

enum
{
  KEY_DOWN=4096,
  KEY_UP,
  KEY_PPAGE,
  KEY_NPAGE,
  KEY_RIGHT,
  KEY_LEFT,
  KEY_HOME,
  KEY_END,
  KEY_IC,
  KEY_DC,
  KEY_F1,
  KEY_F2,
  KEY_F3,
  KEY_F4,
  KEY_F5,
  KEY_F6,
  KEY_F7,
  KEY_F8,
  KEY_F9,
  KEY_F10,
  KEY_F11,
  KEY_F12,
};

#define KEY_BACKSPACE '\b'

#define KEY_F(x) (KEY_F1 + (x) - 1)


typedef struct 
{
  HWND m_hwnd;
  int lines, cols;

  int m_cursor_x;
  int m_cursor_y;
  char m_cur_attr, m_cur_erase_attr;
  unsigned char *m_framebuffer;
  HFONT mOurFont,mOurFont_ul;
  int m_font_w, m_font_h;
  int m_need_redraw;

  WDL_Queue *m_kbq;
  int m_intimer;

  int colortab[COLOR_PAIRS << NUM_ATTRBITS][2];
} win32CursesCtx;

void __init_pair(win32CursesCtx *ctx, int p, int b, int f);

#endif

#endif
