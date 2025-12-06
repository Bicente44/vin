/*
 * screen.h
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose:
 */

#ifndef SCREEN_H
#define SCREEN_H

#include "gapbuffer.h"
#include <time.h>

/* Undo/Redo support */
typedef enum {
    UNDO_INSERT,    /* Characters were inserted */
    UNDO_DELETE     /* Characters were deleted */
} UndoType;

typedef struct {
    UndoType type;
    size_t position;        /* Where action happened */
    char *data;             /* Inserted/deleted characters */
    size_t data_len;        /* Length of data */
} UndoAction;

typedef struct {
    UndoAction *actions;
    int capacity;
    int count;              /* Total actions recorded */
    int current;            /* Current position (for redo) */
} UndoStack;

typedef struct {
    /* Display settings */
    int line_numbers;
    int tab_width;
    int status_timeout;
    /* Keybindings */
    int key_quit;
    int key_save;
    int key_undo;
    int key_redo;
    int key_search;
    int key_delete_line;
    int key_jump_top;
    int key_jump_bottom;
    int key_toggle_line_numbers;
} Config;

typedef struct EditorState {
	GapBuffer *buffer;
	char *filename;
	int term_rows, term_cols;
	size_t cursor_index;
	int cx;
       	int cy;
	int row_offset;
	int col_offset;
	int dirty;
	char status_msg[80];
	time_t status_time;
	int expect_quit;
	char search_query[256];
	int search_mode; /* 0=no search, 1=query, 2=jump matches */
	size_t last_match_index;
	int prompt_mode;
	char prompt_buffer[256];
	UndoStack *undo_stack;
	int show_line_numbers;
	int line_number_width;
	Config *config;
} EditorState;

EditorState *editor_new(GapBuffer *g, const char *filename);
void editor_free(EditorState *E);

int editor_update_window_size(EditorState *E);

void editor_refresh(EditorState *E);

void editor_move_cursor(EditorState *E, int delta);          /* relative */
int editor_set_cursor_index(EditorState *E, size_t index);   /* absolute */
void editor_move_up_down(EditorState *E, int dir);

void editor_set_status(EditorState *E, const char *fmt, ...);

size_t rowcol_to_index(const GapBuffer *g, int target_row, int target_col);

int handle_key(EditorState *E, int key);

/* Debug */



#endif

