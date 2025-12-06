/*
 * terminal.c
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose: terminal raw mode and window size helpers
 */

#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>

static struct termios orig_termios;
static struct termios raw;
static volatile sig_atomic_t winch_flag = 0;
static int raw_enabled = 0;
static int atexit_registered = 0;

/* expose current winch state */
int terminal_was_resized(void) { 
    return winch_flag != 0;
}
void terminal_clear_winch(void) {
    winch_flag = 0;
}

static void sigwinch_handler(int signo) {
    (void)signo;
    winch_flag = 1;
}

/* Install the SIGWINCH handler. It's safe to call multiple times. */
void install_sigwinch_handler(void) {
    if (signal(SIGWINCH, sigwinch_handler) == SIG_ERR) {
        perror("signal(SIGWINCH)");
    }
}

/* restore terminal to original state (atexit handler) */
void disable_raw_mode(void) {
    if (!raw_enabled)
        return;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        /* We can't do much in atexit; print a diagnostic */
        perror("tcsetattr restore");
    }
    raw_enabled = 0;
}

/* Function to enable 'Raw' mode otherwise known as non-canonical mode */
int enable_raw_mode(void) {
    if (raw_enabled)
        return 0;

    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        perror("tcgetattr");
        return 1;
    }

    raw = orig_termios;

    /* Disable canonical mode and echo. Leave ISIG enabled so Ctrl-C still works.
       IEXTEN left disabled as before (often needed on macOS/Linux differences). */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* input modes: turn off flow control, CR-to-NL, strip, parity checks */
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    /* output modes: disable post-processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes: 8-bit chars */
    raw.c_cflag |= (CS8);

    /* Timed read: VMIN=0, VTIME=1 -> read() returns 0 if no bytes within 0.1s */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return 1;
    }

    raw_enabled = 1;

    /* Install SIGWINCH handler so main loop can react to terminal resize */
    install_sigwinch_handler();

    /* Register atexit handler only once */
    if (!atexit_registered) {
        if (atexit(disable_raw_mode) != 0) {
            perror("atexit");
            /* not fatal, continue */
        } else {
            atexit_registered = 1;
        }
    }
    return 0;
}

/* Function to get terminal size (rows, cols). Returns 0 on success, -1 on failure.
   Tries STDOUT_FILENO first, then STDIN_FILENO for robustness. */
int get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (rows)
        *rows = 0;
    if (cols)
        *cols = 0;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
            return -1;
        }
    }
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
    return 0;
}
