/*
 * input.c
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 */

/* src/input.c — minimal read_key stub for debug */
#include "input.h"
#include <unistd.h>   /* read */
#include <errno.h>


static int read_byte(unsigned char *out) {
    while (1) {
        ssize_t n = read(STDIN_FILENO, out, 1);
        if (n == 1) return 1;
        if (n == 0) return 0;
        if (n == -1) {
            if (errno == EINTR) continue; /* try again */
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
    }
}

int read_key(void) {
	unsigned char c;
	int r = read_byte(&c);
	if (r <= 0) return KEY_NULL;

	/* normal keys */
	if (c != '\x1b') {
		if (c == 127) return KEY_BACKSPACE;
		return (int)c;
	}

	/* Esc sequences */
	unsigned char seq[3];
	if (read_byte(&seq[0]) <= 0) {
		return 27;
	}
	if (read_byte(&seq[1]) <= 0) {
		return 27;
	}

	if (seq[0] == '[') {
		if (seq[1] >= '0' && seq[1] <= '9') {
			/* extend sequence */
			if (read_byte(&seq[2]) <= 0) return KEY_NULL;
			if (seq[2] == '~') {
				switch (seq[1]) {
					case '1': return KEY_HOME;
					case '3': return KEY_DELETE;
					case '4': return KEY_END;
					case '5': return KEY_PAGE_UP;
					case '6': return KEY_PAGE_DOWN;
				}
			}
		} else {
			switch (seq[1]) {
				case 'A': return KEY_ARROW_UP;
				case 'B': return KEY_ARROW_DOWN;
				case 'C': return KEY_ARROW_RIGHT;
				case 'D': return KEY_ARROW_LEFT;
				case 'H': return KEY_HOME;
				case 'F': return KEY_END;
			}
		}
	} else if (seq[0] == 'O') {
		switch (seq[1]) {
			case 'H': return KEY_HOME;
			case 'F': return KEY_END;
		}
	}
	/* Unrecognized sequence return esc */
	return 27;
}
