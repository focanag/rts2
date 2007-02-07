#include "rts2ncomwin.h"

#include "nmonitor.h"

Rts2NComWin::Rts2NComWin (WINDOW * master_window):Rts2NWindow (master_window, 11, LINES - 24, COLS - 12, 5,
	     0)
{
  comwin = newpad (1, 300);
  statuspad = newpad (3, 300);
}

Rts2NComWin::~Rts2NComWin (void)
{
  delwin (comwin);
  delwin (statuspad);
}

int
Rts2NComWin::injectKey (int key)
{
  int x, y;
  switch (key)
    {
    case KEY_BACKSPACE:
      getyx (comwin, y, x);
      mvwdelch (comwin, y, x - 1);
      break;
    case KEY_ENTER:
    case K_ENTER:
      return 0;
    default:
      waddch (comwin, key);
    }
  return -1;
}

void
Rts2NComWin::draw ()
{
  Rts2NWindow::draw ();
  refresh ();
}

void
Rts2NComWin::refresh ()
{
  int x, y;
  int w, h;
  mvwprintw (window, 0, 0, "xxxxx");
  mvwprintw (window, 1, 1, "xxxxx");
  Rts2NWindow::refresh ();
  getbegyx (window, y, x);
  getmaxyx (window, h, w);
  if (pnoutrefresh (comwin, 0, 0, y, x, y + 1, x + w - 1) == ERR)
    errorMove ("pnoutrefresh comwin");
  if (pnoutrefresh (statuspad, 0, 0, y + 1, x, y + h - 1, x + w - 1) == ERR)
    errorMove ("pnoutrefresh statuspad");
}

void
Rts2NComWin::setCursor ()
{
  int x, y;
  getyx (comwin, y, x);
  x += getX ();
  y += getY ();
  setsyx (y, x);
}

void
Rts2NComWin::commandReturn (Rts2Command * cmd, int cmd_status)
{
  if (cmd_status == 0)
    wcolor_set (statuspad, CLR_OK, NULL);
  else
    wcolor_set (statuspad, CLR_FAILURE, NULL);
  mvwprintw (statuspad, 1, 0, "%s %+04i %s",
	     cmd->getConnection ()->getName (), cmd_status, cmd->getText ());
  wcolor_set (statuspad, CLR_DEFAULT, NULL);
  wmove (comwin, 0, 0);
}
