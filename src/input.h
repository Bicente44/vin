/*
 * input.h
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 */

#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

/* Key codes: use enum to avoid relocations from const ints */
enum {
	KEY_NULL = 0,
	KEY_CTRL_A = 1,
	KEY_CTRL_B = 2,
	KEY_CTRL_C = 3,
	KEY_CTRL_D = 4,
	KEY_CTRL_E = 5,
	KEY_CTRL_F = 6,
	KEY_CTRL_G = 7,
	KEY_CTRL_H = 8,
	KEY_CTRL_I = 9,
	KEY_CTRL_J = 10,
	KEY_CTRL_K = 11,
	KEY_CTRL_L = 12,
	KEY_CTRL_M = 13,
	KEY_CTRL_N = 14,
	KEY_CTRL_O = 15,
	KEY_CTRL_P = 16,
	KEY_CTRL_Q = 17,
	KEY_CTRL_R = 18,
	KEY_CTRL_S = 19,
	KEY_CTRL_T = 20,
	KEY_CTRL_U = 21,
	KEY_CTRL_V = 22,
	KEY_CTRL_W = 23,
	KEY_CTRL_X = 24,
	KEY_CTRL_Y = 25,
	KEY_CTRL_Z = 26,
	KEY_CTRL_ESC = 27,
	KEY_BACKSPACE = 127,
	
	KEY_ARROW_LEFT = 1000,
	KEY_ARROW_RIGHT,
	KEY_ARROW_UP,
	KEY_ARROW_DOWN,
	KEY_DELETE,
	KEY_HOME,
	KEY_END,
	KEY_PAGE_UP,
	KEY_PAGE_DOWN,
	KEY_INSERT,
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
	KEY_F12
};

int read_key(void);

#endif

