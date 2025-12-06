/*
 * main.c
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose: parses argv and prints / processes usage and runs the editor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "terminal.h"
#include "input.h"
#include "screen.h"
#include "fileio.h"
#include "gapbuffer.h"

/* Enter alternate screen buffer */
static void enter_alternate_screen(void) {
    const char *seq = "\x1b[?1049h";
    write(STDOUT_FILENO, seq, strlen(seq));
}

/* Leave alternate screen buffer */
static void leave_alternate_screen(void) {
    const char *seq = "\x1b[?1049l";
    write(STDOUT_FILENO, seq, strlen(seq));
}

/* Cleanup function to ensure terminal is restored on exit */
static void cleanup_on_exit(void) {
    leave_alternate_screen();
}

static void print_usage(const char *prog) {
    fprintf(stderr, "vin - Lightweight terminal text editor v1.0.0\n");
    fprintf(stderr, "\nUsage:\n");
    fprintf(stderr, "  %s [FILE]\n", prog);
    fprintf(stderr, "  %s --help\n", prog);
    fprintf(stderr, "  %s --version\n", prog);
    
    fprintf(stderr, "\nFile Operations:\n");
    fprintf(stderr, "  CTRL-S         Save file\n");
    fprintf(stderr, "  CTRL-Q         Quit (prompts if unsaved)\n");
    
    fprintf(stderr, "\nEditing:\n");
    fprintf(stderr, "  CTRL-U         Undo\n");
    fprintf(stderr, "  CTRL-R         Redo\n");
    fprintf(stderr, "  CTRL-D         Delete line\n");
    fprintf(stderr, "  Backspace      Delete before cursor\n");
    fprintf(stderr, "  Delete         Delete at cursor\n");
    
    fprintf(stderr, "\nNavigation:\n");
    fprintf(stderr, "  Arrow Keys     Move cursor\n");
    fprintf(stderr, "  CTRL-G         Jump to top\n");
    fprintf(stderr, "  CTRL-B         Jump to bottom\n");
    fprintf(stderr, "  Home           Start of line\n");
    fprintf(stderr, "  End            End of line\n");
    fprintf(stderr, "  Page Up/Down   Scroll by screen\n");
    
    fprintf(stderr, "\nSearch:\n");
    fprintf(stderr, "  CTRL-F         Search (n=next, N=previous, ESC=exit)\n");
    
    fprintf(stderr, "\nConfiguration:\n");
    fprintf(stderr, "  Edit ~/.vinrc to customize settings and keybindings\n");
    fprintf(stderr, "\nFor more information, see: https://github.com/bicente44/vin\n");
}

/* SIGINT handler to ensure clean exit on Ctrl-C */
static void sigint_handler(int signo) {
    (void)signo;
    cleanup_on_exit();
    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}

int main(int argc, char *argv[]) {
    int debug_mode = 0;
    const char *filename = NULL;

    /* Parse arguments */
    if (argc >= 2) {
        if (strcmp(argv[1], "--debug") == 0 || strcmp(argv[1], "-d") == 0) {
            debug_mode = 1;
            if (argc >= 3) filename = argv[2];
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            filename = argv[1];
        }
    }

    /* Enable raw mode */
    if (enable_raw_mode() != 0) {
        fprintf(stderr, "Failed to enable raw mode\n");
        return 1;
    }

    /* Register cleanup handlers */
    if (atexit(cleanup_on_exit) != 0) {
        perror("atexit");
    }
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal(SIGINT)");
    }

    /* Install SIGWINCH handler for terminal resize */
    install_sigwinch_handler();

    /* Enter alternate screen */
    enter_alternate_screen();

    /* Create and populate gapbuffer */
    GapBuffer *g = NULL;
    if (debug_mode) {
        const char *sample = "Hello world\nThis is line two\nThird line\nA longer line to test horizontal scrolling\n";
        g = gb_new(1024);
        if (!g) {
            fprintf(stderr, "Failed to allocate gapbuffer\n");
            cleanup_on_exit();
            return 1;
        }
        if (gb_insert_bytes(g, sample, strlen(sample)) != 0) {
            fprintf(stderr, "Failed to populate gapbuffer\n");
            gb_free(g);
            cleanup_on_exit();
            return 1;
        }
    } else {
        g = gb_new(1024);
        if (!g) {
            fprintf(stderr, "Failed to allocate gapbuffer\n");
            cleanup_on_exit();
            return 1;
        }
        if (filename) {
		if (file_load_to_gapbuffer(filename, g) != 0) {
		}
        }
    }

    /* Create editor state */
    EditorState *E = editor_new(g, filename ? filename : (debug_mode ? "debug" : NULL));
    if (!E) {
        fprintf(stderr, "Failed to create editor state\n");
        gb_free(g);
        cleanup_on_exit();
        return 1;
    }

    /* Initialize viewport and cursor */
    E->row_offset = 0;
    E->col_offset = 0;
    editor_set_cursor_index(E, 0);

    /* Initial render */
    editor_refresh(E);

    /* Main event loop */
    for (;;) {
        int k = read_key();

        /* Handle timeout (resize check) */
        if (k == KEY_NULL) {
            if (terminal_was_resized()) {
    		editor_update_window_size(E);
                terminal_clear_winch();
                editor_refresh(E);
            }
            continue;
        }

        /* Handle key (returns 1 to quit) */
        if (handle_key(E, k)) {
            break;
        }

        /* Check resize again */
        if (terminal_was_resized()) {
            editor_update_window_size(E);
            terminal_clear_winch();
        }

        /* Redraw */
        editor_refresh(E);
    }

    /* Cleanup */
    editor_free(E);
    gb_free(g);
    leave_alternate_screen();

    return 0;
}
