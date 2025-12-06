/*
 * terminal.h
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose:
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <sys/types.h>
#include <signal.h>

int terminal_was_resized(void); /* returns non-zero if SIGWINCH fired */
void terminal_clear_winch(void); /* clear the flag after handling */

int enable_raw_mode(void);    /* returns 0 on success */
void disable_raw_mode(void);  /* restore original state */

int get_window_size(int *rows, int *cols); /* returns 0 on success */

/* call at startup to set up signal handler for SIGWINCH */
void install_sigwinch_handler(void);

#endif
