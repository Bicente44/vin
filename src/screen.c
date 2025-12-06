/*
 * screen.c
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose: terminal rendering and editor viewport / cursor mapping
 */

#include "screen.h"
#include "terminal.h"
#include "input.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <limits.h> /* for INT_MAX used by END handling */

#define ESC "\x1b"
#define CURSOR_HIDE ESC "[?25l"
#define CURSOR_SHOW ESC "[?25h"
#define CLEAR_SCREEN ESC "[2J"
#define MOVE_CURSOR_FMT ESC "[%d;%dH" /* printf style: row,col (1-based) */
#define CLEAR_LINE ESC "[K"
#define INVERT_COLORS ESC "[7m"
#define RESET_COLORS ESC "[0m"
#define TIME_OUT 2
#define UNDO_CAPACITY 100
#define MAX_SEARCH_LEN 256

/* -------------------------
   Helper: row/line calculations
   ------------------------- */

/* Find the byte index of the first character of target_row.
   If target_row == 0 => return 0.
   If target_row > last row, return len (EOF).
*/
static size_t find_row_start(const GapBuffer *g, int target_row) {
    size_t i = 0;
    int row = 0;
    size_t len = gb_len(g);

    if (target_row == 0)
        return 0;

    while (i < len && row < target_row) {
        if (gb_char_at(g, i) == '\n') {
            row++;
            if (row == target_row) {
                return i + 1;
            }
        }
        i++;
    }

    return len;
}

/* Calculate how wide the line number column should be */
static int calculate_line_number_width(const GapBuffer *g) {
    /* Count total lines in buffer */
    size_t len = gb_len(g);
    int lines = 1; /* At least 1 line */

    for (size_t i = 0; i < len; i++) {
        if (gb_char_at(g, i) == '\n')
            lines++;
    }

    /* Calculate width needed for line numbers */
    int width = 1; /* Minimum 1 digit */
    int temp = lines;
    while (temp >= 10) {
        width++;
        temp /= 10;
    }

    return width + 1; /* +1 for space after number */
}

/* Convert byte index -> row, col (0-based). If index == len, returns row/col at EOF. */
void index_to_rowcol(const GapBuffer *g, size_t index, int *out_row, int *out_col) {
    size_t i;
    int row = 0, col = 0;
    size_t len = gb_len(g);
    if (index > len)
        index = len;
    for (i = 0; i < index; ++i) {
        char c = gb_char_at(g, i);
        if (c == '\n') {
            row++;
            col = 0;
        } else {
            col++;
        }
    }
    if (out_row)
        *out_row = row;
    if (out_col)
        *out_col = col;
}

/* Convert row/col -> byte index.
   If target_row beyond EOF, returns len.
   If target_col beyond line length, returns index at line end (before newline or EOF).
*/
size_t rowcol_to_index(const GapBuffer *g, int target_row, int target_col) {
    if (target_row <= 0 && target_col <= 0)
        return 0;
    size_t len = gb_len(g);
    size_t i = find_row_start(g, target_row);
    if (i >= len)
        return len;

    int col = 0;
    while (i < len) {
        char c = gb_char_at(g, i);
        if (c == '\n') {
            return i;
        }
        if (col == target_col)
            break;
        ++col;
        ++i;
    }
    return i;
}

/* -------------------------
   Small string builder for single write
   ------------------------- */
static void buf_append(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t sl = strlen(s);
    size_t need = *len + sl;
    if (need + 1 > *cap) {
        size_t newcap = *cap ? *cap * 2 : 1024;
        while (newcap < need + 1)
            newcap *= 2;
        char *p = realloc(*buf, newcap);
        if (!p)
            return;
        *buf = p;
        *cap = newcap;
    }
    memcpy(*buf + *len, s, sl);
    *len += sl;
    (*buf)[*len] = '\0';
}

static void buf_appendf(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        buf_append(buf, len, cap, tmp);
        return;
    }
    char *big = malloc((size_t)n + 1);
    if (!big)
        return;
    va_start(ap, fmt);
    vsnprintf(big, n + 1, fmt, ap);
    va_end(ap);
    buf_append(buf, len, cap, big);
    free(big);
}

/* -------------------------
   Viewport / rendering helpers
   ------------------------- */

/* Ensure the logical cursor is visible by adjusting row_offset/col_offset */
void ensure_cursor_visible(EditorState *E) {
    if (!E || !E->buffer)
        return;

    int row = 0, col = 0;
    index_to_rowcol(E->buffer, E->cursor_index, &row, &col);

    int content_rows = E->term_rows > 1 ? E->term_rows - 1 : 1;

    /* Vertical scrolling */
    if (row < E->row_offset) {
        E->row_offset = row;
        if (E->row_offset < 0)
            E->row_offset = 0;
    } else if (row >= E->row_offset + content_rows) {
        E->row_offset = row - content_rows + 1;
        if (E->row_offset < 0)
            E->row_offset = 0;
    }

    /* Horizontal scrolling */
    if (col < E->col_offset) {
        E->col_offset = col;
        if (E->col_offset < 0)
            E->col_offset = 0;
    } else if (col >= E->col_offset + E->term_cols) {
        E->col_offset = col - E->term_cols + 1;
        if (E->col_offset < 0)
            E->col_offset = 0;
    }
}

/* Render a single visible row */
static void render_row(EditorState *E, int vis_row, char **out, size_t *out_len, size_t *out_cap) {
    if (!E || !E->buffer)
        return;

    int target_row = E->row_offset + vis_row;
    size_t len = gb_len(E->buffer);
    size_t start = find_row_start(E->buffer, target_row);

    /* Calculate actual line number (1-based) */
    int line_number = target_row + 1;

    /* Calculate available columns for content */
    int termcols = E->term_cols > 0 ? E->term_cols : 80;
    int content_cols = termcols - E->line_number_width;

    /* Draw line number if enabled */
    if (E->show_line_numbers && E->line_number_width > 0) {
        /* A line is real if start is within the buffer bounds */
        int show_number = (start < len);

        /* Special case: if we're exactly at EOF and the last char was a newline,
           this is a valid empty line (cursor is here after pressing Enter) */
        if (!show_number && start == len && len > 0) {
            /* Only show if we're on the FIRST line after EOF, not multiple lines */
            if (target_row == 0 || find_row_start(E->buffer, target_row - 1) < len) {
                if (gb_char_at(E->buffer, len - 1) == '\n') {
                    show_number = 1;
                }
            }
        }

        if (show_number) {
            /* Real line - show line number */
            char line_num_str[16];
            snprintf(line_num_str, sizeof(line_num_str), "%*d ",
                     E->line_number_width - 1, line_number);
            buf_append(out, out_len, out_cap, line_num_str);
        } else {
            /* Past EOF - show tilde (vim style) or spaces */
            buf_append(out, out_len, out_cap, "~");
            for (int i = 1; i < E->line_number_width; i++) {
                buf_append(out, out_len, out_cap, " ");
            }
        }
    }

    /* Empty row past EOF - check if this is beyond actual content */
    int is_past_eof = 0;
    if (start >= len) {
        /* We're past EOF unless we're on the first empty line after a newline */
        is_past_eof = 1;
        if (start == len && len > 0 && target_row > 0) {
            if (find_row_start(E->buffer, target_row - 1) < len &&
                gb_char_at(E->buffer, len - 1) == '\n') {
                is_past_eof = 0; /* This is a valid empty line */
            }
        }
    }

    if (is_past_eof) {
        if (content_cols <= 256) {
            char sp[257];
            memset(sp, ' ', content_cols);
            sp[content_cols] = '\0';
            buf_append(out, out_len, out_cap, sp);
        } else {
            char *sp = malloc(content_cols + 1);
            if (sp) {
                memset(sp, ' ', content_cols);
                sp[content_cols] = '\0';
                buf_append(out, out_len, out_cap, sp);
                free(sp);
            }
        }
        buf_append(out, out_len, out_cap, "\r\n");
        return;
    }
    /* Render line content */
    int col = 0;
    size_t i = start;
    int printed = 0;

    while (i < len) {
        char ch = gb_char_at(E->buffer, i);
        if (ch == '\n')
            break;
        if (col >= E->col_offset) {
            if (printed >= content_cols)
                break;
            char tmp[2] = {ch, '\0'};
            buf_append(out, out_len, out_cap, tmp);
            ++printed;
        }
        ++col;
        ++i;
    }

    /* Pad to full width */
    if (printed < content_cols) {
        int pad = content_cols - printed;
        if (pad <= 256) {
            char sp[257];
            memset(sp, ' ', pad);
            sp[pad] = '\0';
            buf_append(out, out_len, out_cap, sp);
        } else {
            char *sp = malloc(pad + 1);
            if (sp) {
                memset(sp, ' ', pad);
                sp[pad] = '\0';
                buf_append(out, out_len, out_cap, sp);
                free(sp);
            }
        }
    }

    buf_append(out, out_len, out_cap, "\r\n");
}

/* Create a new undo stack */
static UndoStack *undo_stack_new(void) {
    UndoStack *stack = malloc(sizeof(UndoStack));
    if (!stack)
        return NULL;

    stack->capacity = UNDO_CAPACITY;
    stack->actions = malloc(sizeof(UndoAction) * stack->capacity);
    if (!stack->actions) {
        free(stack);
        return NULL;
    }

    stack->count = 0;
    stack->current = 0;
    return stack;
}

/* Free undo stack */
static void undo_stack_free(UndoStack *stack) {
    if (!stack)
        return;

    /* Free all action data */
    for (int i = 0; i < stack->count; i++) {
        free(stack->actions[i].data);
    }

    free(stack->actions);
    free(stack);
}

/* Push an action onto the undo stack */
static int undo_stack_push(UndoStack *stack, UndoType type, size_t position, const char *data, size_t data_len) {
    if (!stack || !data)
        return -1;

    /* If we're not at the end (user did undo then typed), discard redo history */
    if (stack->current < stack->count) {
        /* Free actions after current */
        for (int i = stack->current; i < stack->count; i++) {
            free(stack->actions[i].data);
        }
        stack->count = stack->current;
    }

    /* Grow if needed */
    if (stack->count >= stack->capacity) {
        int new_cap = stack->capacity * 2;
        UndoAction *new_actions = realloc(stack->actions, sizeof(UndoAction) * new_cap);
        if (!new_actions)
            return -1;
        stack->actions = new_actions;
        stack->capacity = new_cap;
    }

    /* Allocate and copy data */
    char *data_copy = malloc(data_len);
    if (!data_copy)
        return -1;
    memcpy(data_copy, data, data_len);

    /* Add action */
    stack->actions[stack->count].type = type;
    stack->actions[stack->count].position = position;
    stack->actions[stack->count].data = data_copy;
    stack->actions[stack->count].data_len = data_len;

    stack->count++;
    stack->current = stack->count;

    return 0;
}

/* ------------------------
   Config system
   ------------------------ */
static int parse_key_string(const char *str) {
    if (strcmp(str, "CTRL_A") == 0)
        return KEY_CTRL_A;
    if (strcmp(str, "CTRL_B") == 0)
        return KEY_CTRL_B;
    if (strcmp(str, "CTRL_C") == 0)
        return KEY_CTRL_C;
    if (strcmp(str, "CTRL_D") == 0)
        return KEY_CTRL_D;
    if (strcmp(str, "CTRL_E") == 0)
        return KEY_CTRL_E;
    if (strcmp(str, "CTRL_F") == 0)
        return KEY_CTRL_F;
    if (strcmp(str, "CTRL_G") == 0)
        return KEY_CTRL_G;
    if (strcmp(str, "CTRL_H") == 0)
        return KEY_CTRL_H;
    if (strcmp(str, "CTRL_I") == 0)
        return KEY_CTRL_I;
    if (strcmp(str, "CTRL_J") == 0)
        return KEY_CTRL_J;
    if (strcmp(str, "CTRL_K") == 0)
        return KEY_CTRL_K;
    if (strcmp(str, "CTRL_L") == 0)
        return KEY_CTRL_L;
    if (strcmp(str, "CTRL_M") == 0)
        return KEY_CTRL_M;
    if (strcmp(str, "CTRL_N") == 0)
        return KEY_CTRL_N;
    if (strcmp(str, "CTRL_O") == 0)
        return KEY_CTRL_O;
    if (strcmp(str, "CTRL_P") == 0)
        return KEY_CTRL_P;
    if (strcmp(str, "CTRL_Q") == 0)
        return KEY_CTRL_Q;
    if (strcmp(str, "CTRL_R") == 0)
        return KEY_CTRL_R;
    if (strcmp(str, "CTRL_S") == 0)
        return KEY_CTRL_S;
    if (strcmp(str, "CTRL_T") == 0)
        return KEY_CTRL_T;
    if (strcmp(str, "CTRL_U") == 0)
        return KEY_CTRL_U;
    if (strcmp(str, "CTRL_V") == 0)
        return KEY_CTRL_V;
    if (strcmp(str, "CTRL_W") == 0)
        return KEY_CTRL_W;
    if (strcmp(str, "CTRL_X") == 0)
        return KEY_CTRL_X;
    if (strcmp(str, "CTRL_Y") == 0)
        return KEY_CTRL_Y;
    if (strcmp(str, "CTRL_Z") == 0)
        return KEY_CTRL_Z;

    /* Single character keys */
    if (strlen(str) == 1)
        return (int)str[0];

    return -1; /* Unknown */
}

/* Trim whitespace from start and end of string */
static void trim_whitespace(char *str) {
    if (!str)
        return;

    /* Trim leading whitespace */
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t'))
        start++;

    /* Move string to beginning if needed */
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    /* Trim trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
}

/* Load config from file */
static void config_load(Config *cfg, const char *filepath) {
    if (!cfg || !filepath)
        return;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        /* Parse key=value */
        char *equals = strchr(line, '=');
        if (!equals)
            continue;

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;

        trim_whitespace(key);
        trim_whitespace(value);

        /* Parse display settings */
        if (strcmp(key, "line_numbers") == 0) {
            cfg->line_numbers = (strcmp(value, "on") == 0) ? 1 : 0;
        } else if (strcmp(key, "tab_width") == 0) {
            cfg->tab_width = atoi(value);
        } else if (strcmp(key, "status_timeout") == 0) {
            cfg->status_timeout = atoi(value);
        }
        /* Parse keybindings */
        else if (strcmp(key, "quit") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_quit = parsed;
        } else if (strcmp(key, "save") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_save = parsed;
        } else if (strcmp(key, "undo") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_undo = parsed;
        } else if (strcmp(key, "redo") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_redo = parsed;
        } else if (strcmp(key, "search") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_search = parsed;
        } else if (strcmp(key, "delete_line") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_delete_line = parsed;
        } else if (strcmp(key, "jump_top") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_jump_top = parsed;
        } else if (strcmp(key, "jump_bottom") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_jump_bottom = parsed;
        } else if (strcmp(key, "toggle_line_numbers") == 0) {
            int parsed = parse_key_string(value);
            if (parsed != -1)
                cfg->key_toggle_line_numbers = parsed;
        }
    }

    fclose(f);
}

/* Create default config */
static Config *config_new_default(void) {
    Config *cfg = malloc(sizeof(Config));
    if (!cfg)
        return NULL;

    /* Display defaults */
    cfg->line_numbers = 1;
    cfg->tab_width = 4;
    cfg->status_timeout = 2;

    /* Keybinding defaults */
    cfg->key_quit = KEY_CTRL_Q;
    cfg->key_save = KEY_CTRL_S;
    cfg->key_undo = KEY_CTRL_U;
    cfg->key_redo = KEY_CTRL_R;
    cfg->key_search = KEY_CTRL_F;
    cfg->key_delete_line = KEY_CTRL_D;
    cfg->key_jump_top = KEY_CTRL_G;
    cfg->key_jump_bottom = KEY_CTRL_B;

    return cfg;
}

/* Free config */
static void config_free(Config *cfg) {
    if (cfg)
        free(cfg);
}

/* -------------------------
   Editor state management
   ------------------------- */

EditorState *editor_new(GapBuffer *g, const char *filename) {
    int rows, cols;
    EditorState *E = malloc(sizeof *E);
    if (!E)
        return NULL;

    memset(E, 0, sizeof *E);
    E->buffer = g;
    E->filename = NULL;
    if (filename) {
        E->filename = malloc(strlen(filename) + 1);
        if (E->filename)
            strcpy(E->filename, filename);
    }
    if (get_window_size(&rows, &cols) == -1) {
        rows = 24;
        cols = 80;
    }
    E->term_rows = rows;
    E->term_cols = cols;

    E->row_offset = 0;
    E->col_offset = 0;
    E->dirty = 0;
    E->status_msg[0] = '\0';
    E->status_time = 0;
    E->expect_quit = 0;
    E->cursor_index = 0;
    E->cx = 0;
    E->cy = 0;

    E->search_query[0] = '\0';
    E->search_mode = 0;
    E->last_match_index = 0;
    E->prompt_mode = 0;
    E->prompt_buffer[0] = '\0';

    E->config = config_new_default();
    if (!E->config) {
        if (E->filename)
            free(E->filename);
        free(E);
        return NULL;
    }

    /* Try to load from ~/.vinrc */
    char config_path[512];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(config_path, sizeof(config_path), "%s/.vinrc", home);
        config_load(E->config, config_path);
    }

    E->show_line_numbers = 1;
    E->line_number_width = 0;
    E->undo_stack = undo_stack_new();
    if (!E->undo_stack) {
        free(E);
        return NULL;
    }
    return E;
}

void editor_free(EditorState *E) {
    if (!E)
        return;
    if (E->filename)
        free((void *)E->filename);
    undo_stack_free(E->undo_stack);
    config_free(E->config);
    free(E);
}

int editor_update_window_size(EditorState *E) {
    int rows, cols;
    if (get_window_size(&rows, &cols) == -1)
        return -1;
    E->term_rows = rows;
    E->term_cols = cols;
    ensure_cursor_visible(E);
    return 0;
}

void editor_refresh(EditorState *E) {
    if (!E || !E->buffer)
        return;
    get_window_size(&E->term_rows, &E->term_cols);

    if (E->term_rows <= 0 || E->term_cols <= 0)
        editor_update_window_size(E);

    ensure_cursor_visible(E);

    if (E->show_line_numbers) {
        E->line_number_width = calculate_line_number_width(E->buffer);
    } else {
        E->line_number_width = 0;
    }

    char *out = NULL;
    size_t out_len = 0, out_cap = 0;

    /* Hide cursor, clear screen, move to home */
    buf_append(&out, &out_len, &out_cap, CURSOR_HIDE);
    buf_append(&out, &out_len, &out_cap, ESC "[H");
    buf_append(&out, &out_len, &out_cap, CLEAR_SCREEN);
    buf_appendf(&out, &out_len, &out_cap, MOVE_CURSOR_FMT, 1, 1);

    if (E->show_line_numbers) {
        E->line_number_width = calculate_line_number_width(E->buffer);
    } else {
        E->line_number_width = 0;
    }

    /* Render visible lines */
    int visible_rows = E->term_rows > 1 ? E->term_rows - 1 : 1;
    for (int r = 0; r < visible_rows; ++r) {
        render_row(E, r, &out, &out_len, &out_cap);
    }

    /* Status bar */
    char status[512];
    time_t now = time(NULL);
    int cols = E->term_cols > 0 ? E->term_cols : 80;
    int n = 0;
    const int STATUS_TIMEOUT = TIME_OUT;
    if (E->status_msg[0] != '\0' && (now - E->status_time) < STATUS_TIMEOUT) {
        /* Show the user's status message (replace normal status). */
        n = snprintf(status, sizeof status, " %s", E->status_msg);
        if (n < 0)
            n = 0;
    } else {
        /* Normal status (filename, Ln/Col, modified flag). */
        n = snprintf(status, sizeof status, " %.20s  Ln %d, Col %d %s",
                     E->filename ? E->filename : "[No Name]",
                     E->cy + 1, E->cx + 1,
                     E->dirty ? "(modified)" : "");
        if (n < 0)
            n = 0;

        /* If there was an expired message, clear it so it won't be considered again */
        if (E->status_msg[0] != '\0' && (now - E->status_time) >= STATUS_TIMEOUT) {
            E->status_msg[0] = '\0';
        }
    }

    /* Pad or truncate to exactly terminal width */
    if (n < cols) {
        if ((size_t)cols < sizeof status) { /* safety */
            memset(status + n, ' ', cols - n);
            status[cols] = '\0';
        } else {
            /* terminal wider than status buffer - cap at buffer */
            status[sizeof status - 1] = '\0';
        }
    } else {
        /* truncate exactly to terminal width */
        if (cols < (int)sizeof status)
            status[cols] = '\0';
        else
            status[sizeof status - 1] = '\0';
    }
    buf_append(&out, &out_len, &out_cap, INVERT_COLORS);
    buf_append(&out, &out_len, &out_cap, status);
    buf_append(&out, &out_len, &out_cap, RESET_COLORS);

    /* Position cursor */
    int screen_r = (E->cy - E->row_offset) + 1;
    int screen_c = (E->cx - E->col_offset) + 1;

    /* Account for line numbers */
    if (E->show_line_numbers) {
        screen_c += E->line_number_width; /* ← Shift cursor right */
    }

    if (screen_r < 1)
        screen_r = 1;
    if (screen_c < 1)
        screen_c = 1;
    if (screen_r > E->term_rows)
        screen_r = E->term_rows;
    if (screen_c > E->term_cols)
        screen_c = E->term_cols;

    buf_appendf(&out, &out_len, &out_cap, MOVE_CURSOR_FMT, screen_r, screen_c);
    buf_append(&out, &out_len, &out_cap, CURSOR_SHOW);

    /* Write to terminal */
    if (out_len > 0)
        write(STDOUT_FILENO, out, out_len);

    free(out);
}

/* -------------------------
   Cursor movement
   ------------------------- */

int editor_set_cursor_index(EditorState *E, size_t index) {
    if (!E || !E->buffer)
        return -1;
    size_t len = gb_len(E->buffer);
    if (index > len)
        index = len;
    E->cursor_index = index;

    (void)gb_move_gap(E->buffer, E->cursor_index);
    index_to_rowcol(E->buffer, E->cursor_index, &E->cy, &E->cx);
    ensure_cursor_visible(E);
    return 0;
}

void editor_move_cursor(EditorState *E, int delta) {
    if (!E || !E->buffer)
        return;
    size_t cur = E->cursor_index;
    if (delta < 0) {
        size_t d = (size_t)(-delta);
        if (d > cur)
            cur = 0;
        else
            cur -= d;
    } else {
        size_t len = gb_len(E->buffer);
        size_t want = cur + (size_t)delta;
        if (want > len)
            want = len;
        cur = want;
    }
    editor_set_cursor_index(E, cur);
}

void editor_move_up_down(EditorState *E, int dir) {
    if (!E || !E->buffer)
        return;
    int row = 0, col = 0;
    index_to_rowcol(E->buffer, E->cursor_index, &row, &col);
    int target_row = row + (dir < 0 ? -1 : 1);
    if (target_row < 0)
        target_row = 0;

    size_t newidx = rowcol_to_index(E->buffer, target_row, col);
    editor_set_cursor_index(E, newidx);
}

void editor_set_status(EditorState *E, const char *fmt, ...) {
    if (!E)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E->status_msg, sizeof E->status_msg, fmt, ap);
    va_end(ap);
    E->status_time = time(NULL);
}

/* Handle keys when in filename prompt mode */
static int handle_filename_prompt(EditorState *E, int key) {
    /* ESC - cancel */
    if (key == 27) {
        E->prompt_mode = 0;
        E->prompt_buffer[0] = '\0';
        editor_set_status(E, "Save cancelled");
        return 0;
    }

    /* Enter - save with entered filename */
    if (key == '\r' || key == '\n') {
        if (E->prompt_buffer[0] == '\0') {
            /* Empty filename - cancel */
            E->prompt_mode = 0;
            editor_set_status(E, "Save cancelled");
            return 0;
        }

        /* Set filename and save */
        E->filename = malloc(strlen(E->prompt_buffer) + 1);
        if (!E->filename) {
            editor_set_status(E, "Out of memory");
            E->prompt_mode = 0;
            return 0;
        }
        strcpy(E->filename, E->prompt_buffer);

        /* Now save */
        if (file_save_from_gapbuffer(E->filename, E->buffer) == 0) {
            E->dirty = 0;
            editor_set_status(E, "Saved \"%s\"", E->filename);
        } else {
            editor_set_status(E, "Save failed for \"%s\"", E->filename);
        }

        E->prompt_mode = 0;
        return 0;
    }

    /* Backspace */
    if (key == KEY_BACKSPACE || key == 127) {
        size_t len = strlen(E->prompt_buffer);
        if (len > 0) {
            E->prompt_buffer[len - 1] = '\0';
            editor_set_status(E, "Save as: %s", E->prompt_buffer);
        }
        return 0;
    }

    /* Printable characters (and some special chars for filenames) */
    if ((key >= 32 && key <= 126) || key == '/' || key == '.') {
        size_t len = strlen(E->prompt_buffer);
        if (len < sizeof(E->prompt_buffer) - 1) {
            E->prompt_buffer[len] = (char)key;
            E->prompt_buffer[len + 1] = '\0';
            editor_set_status(E, "Save as: %s", E->prompt_buffer);
        }
        return 0;
    }

    return 0;
}

/* search handling */
static size_t search_forward(const GapBuffer *g, size_t start_index, const char *query) {
    if (!g || !query || !query[0])
        return (size_t)-1;

    size_t len = gb_len(g);
    size_t query_len = strlen(query);

    if (query_len == 0)
        return (size_t)-1;

    /* Search from start_index to end of buffer */
    for (size_t i = start_index; i + query_len <= len; i++) {
        /* Check if query matches at position i */
        int match = 1;
        for (size_t j = 0; j < query_len; j++) {
            if (gb_char_at(g, i + j) != query[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            return i;
        }
    }

    return (size_t)-1; /* Not found */
}

static size_t search_backward(const GapBuffer *g, size_t start_index, const char *query) {
    if (!g || !query || !query[0])
        return (size_t)-1;

    size_t query_len = strlen(query);
    if (query_len == 0)
        return (size_t)-1;

    /* Can't search before beginning */
    if (start_index < query_len) {
        start_index = 0;
    } else {
        start_index = start_index - query_len;
    }

    /* Search backward from start_index to beginning */
    for (size_t i = start_index; i != (size_t)-1; i--) {
        /* Check if query matches at position i */
        int match = 1;
        for (size_t j = 0; j < query_len; j++) {
            if (gb_char_at(g, i + j) != query[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            return i;
        }

        if (i == 0)
            break; /* Prevent underflow */
    }

    return (size_t)-1; /* Not found */
}

static int handle_search_input(EditorState *E, int key) {
    /* ESC - cancel search */
    if (key == 27) {
        E->search_mode = 0;
        E->search_query[0] = '\0';
        editor_set_status(E, "Search cancelled");
        return 0;
    }

    /* Enter - execute search */
    if (key == '\r' || key == '\n') {
        if (E->search_query[0] == '\0') {
            /* Empty search - cancel */
            E->search_mode = 0;
            editor_set_status(E, "");
            return 0;
        }

        /* Find first match starting from current position */
        size_t match_index = search_forward(E->buffer, E->cursor_index, E->search_query);

        if (match_index == (size_t)-1) {
            /* Not found - try from beginning */
            match_index = search_forward(E->buffer, 0, E->search_query);

            if (match_index == (size_t)-1) {
                editor_set_status(E, "Not found: %s", E->search_query);
                E->search_mode = 0;
            } else {
                editor_set_cursor_index(E, match_index);
                E->last_match_index = match_index;
                E->search_mode = 2; /* Now in active search mode */
                editor_set_status(E, "Found (Ctrl-F for next)");
            }
        } else {
            editor_set_cursor_index(E, match_index);
            E->last_match_index = match_index;
            E->search_mode = 2;
            editor_set_status(E, "Found (Ctrl-F for next)");
        }
        return 0;
    }

    /* Backspace - delete from search string */
    if (key == KEY_BACKSPACE || key == 127) {
        size_t len = strlen(E->search_query);
        if (len > 0) {
            E->search_query[len - 1] = '\0';
            editor_set_status(E, "Search: %s", E->search_query);
        }
        return 0;
    }

    /* Printable character - add to search string */
    if (key >= 32 && key <= 126) {
        size_t len = strlen(E->search_query);
        if (len < sizeof(E->search_query) - 1) {
            E->search_query[len] = (char)key;
            E->search_query[len + 1] = '\0';
            editor_set_status(E, "Search: %s", E->search_query);
        }
        return 0;
    }

    /* Ignore other keys in search mode */
    return 0;
}

/* -------------------------
   Key handling
   ------------------------- */

int handle_key(EditorState *E, int key) {
    if (!E || !E->buffer)
        return 0;

    if (key == 27) {
        if (E->search_mode > 0) {
            E->search_mode = 0;
            E->search_query[0] = '\0';
            editor_set_status(E, "");
            return 0;
        }
        if (E->prompt_mode > 0) {
            E->prompt_mode = 0;
            E->prompt_buffer[0] = '\0';
            editor_set_status(E, "");
            return 0;
        }
        return 0;
    }
    /* Handle filename prompt mode */
    if (E->prompt_mode == 1) {
        return handle_filename_prompt(E, key);
    }

    /* Handle search input mode */
    if (E->search_mode == 1) {
        return handle_search_input(E, key);
    }

    if (key != E->config->key_quit && E->search_mode == 0)
        E->expect_quit = 0;

    /* Quit */
    if (key == E->config->key_quit) {
        if (E->dirty && !E->expect_quit) {
            editor_set_status(E, "Unsaved changes. Press Ctrl-Q again to quit, Ctrl-S to save.");
            E->expect_quit = 1;
            return 0;
        }
        return 1;
    }

    /* Save */
    if (key == E->config->key_save) {
        if (!E->filename) {
            E->prompt_mode = 1;
            E->prompt_buffer[0] = '\0';
            editor_set_status(E, "Save as: ");
            return 0;
        }

        if (file_save_from_gapbuffer(E->filename, E->buffer) == 0) {
            E->dirty = 0;
            editor_set_status(E, "Saved \"%s\"", E->filename);
        } else {
            editor_set_status(E, "Save failed for \"%s\"", E->filename);
        }
        return 0;
    }

    /* Movement */
    if (key == KEY_ARROW_LEFT) {
        editor_move_cursor(E, -1);
        return 0;
    }
    if (key == KEY_ARROW_RIGHT) {
        editor_move_cursor(E, +1);
        return 0;
    }
    if (key == KEY_ARROW_UP) {
        editor_move_up_down(E, -1);
        return 0;
    }
    if (key == KEY_ARROW_DOWN) {
        editor_move_up_down(E, +1);
        return 0;
    }

    /* HOME: go to start of current line */
    if (key == KEY_HOME) {
        /* row = current logical row; col 0 = start of that line */
        size_t idx = rowcol_to_index(E->buffer, E->cy, 0);
        editor_set_cursor_index(E, idx);
        return 0;
    }

    /* END: go to end of current line */
    if (key == KEY_END) {
        /* pass a very large column; rowcol_to_index will clamp to line end */
        size_t idx = rowcol_to_index(E->buffer, E->cy, INT_MAX);
        editor_set_cursor_index(E, idx);
        return 0;
    }

    /* PAGE_UP: move up by one screenful (visible rows) */
    if (key == KEY_PAGE_UP) {
        int visible = (E->term_rows > 1) ? (E->term_rows - 1) : 1;
        int target_row = E->cy - visible;
        if (target_row < 0)
            target_row = 0;
        size_t idx = rowcol_to_index(E->buffer, target_row, E->cx);
        /* set cursor and also nudge viewport so the page-up is visible */
        editor_set_cursor_index(E, idx);
        return 0;
    }

    /* PAGE_DOWN: move down by one screenful (visible rows) */
    if (key == KEY_PAGE_DOWN) {
        int visible = (E->term_rows > 1) ? (E->term_rows - 1) : 1;
        int target_row = E->cy + visible;
        /* rowcol_to_index will clamp if past EOF/last line */
        size_t idx = rowcol_to_index(E->buffer, target_row, E->cx);
        editor_set_cursor_index(E, idx);
        /* move viewport so the cursor is near top of the new page */
        return 0;
    }

    /* Backspace */
    if (key == KEY_BACKSPACE) {
        if (E->cursor_index == 0)
            return 0;

        /* Record what we're about to delete */
        char deleted_char = gb_char_at(E->buffer, E->cursor_index - 1);

        if (gb_move_gap(E->buffer, E->cursor_index) == 0 && gb_delete_before(E->buffer) == 0) {
            /* Record undo action */
            undo_stack_push(E->undo_stack, UNDO_DELETE, E->cursor_index - 1, &deleted_char, 1);
            E->cursor_index = (E->cursor_index > 0) ? E->cursor_index - 1 : 0;
            E->dirty = 1;
            editor_set_cursor_index(E, E->cursor_index);
        }
        return 0;
    }

    /* Delete */
    if (key == KEY_DELETE) {
        char deleted_char = gb_char_at(E->buffer, E->cursor_index);
        if (gb_move_gap(E->buffer, E->cursor_index) == 0 && gb_delete_at(E->buffer) == 0) {
            undo_stack_push(E->undo_stack, UNDO_DELETE, E->cursor_index, &deleted_char, 1);
            E->dirty = 1;
            editor_set_cursor_index(E, E->cursor_index);
        }
        return 0;
    }

    /* Enter */
    if (key == '\r' || key == '\n') {
        if (gb_move_gap(E->buffer, E->cursor_index) == 0 && gb_insert_char(E->buffer, '\n') == 0) {
            char newline = '\n';
            undo_stack_push(E->undo_stack, UNDO_INSERT, E->cursor_index, &newline, 1);
            E->cursor_index += 1;
            E->dirty = 1;
            editor_set_cursor_index(E, E->cursor_index);
        }
        return 0;
    }

    if (key == E->config->key_delete_line) {
        /* Find start and end of current line */
        size_t line_start = find_row_start(E->buffer, E->cy);
        size_t line_end = line_start;
        size_t len = gb_len(E->buffer);

        /* Find the newline at end of line (or EOF) */
        while (line_end < len && gb_char_at(E->buffer, line_end) != '\n') {
            line_end++;
        }

        /* Include the newline in deletion */
        if (line_end < len)
            line_end++;

        size_t delete_len = line_end - line_start;

        if (delete_len == 0)
            return 0; /* Nothing to delete */

        /* Record deleted content for undo */
        char *deleted = malloc(delete_len);
        if (deleted) {
            for (size_t i = 0; i < delete_len; i++) {
                deleted[i] = gb_char_at(E->buffer, line_start + i);
            }
            undo_stack_push(E->undo_stack, UNDO_DELETE, line_start, deleted, delete_len);
            free(deleted);
        }

        /* Delete the line */
        gb_move_gap(E->buffer, line_start);
        for (size_t i = 0; i < delete_len; i++) {
            gb_delete_at(E->buffer);
        }

        /* Move cursor to start of next line (or stay at current position) */
        editor_set_cursor_index(E, line_start);
        E->dirty = 1;
        editor_set_status(E, "Line deleted");
        return 0;
    }

    /* jump to top of file */
    if (key == E->config->key_jump_top) {
        editor_set_cursor_index(E, 0);
        return 0;
    }
    /* jump to bottom of file */
    if (key == E->config->key_jump_bottom) {
        editor_set_cursor_index(E, gb_len(E->buffer));
        return 0;
    }

    /* Undo */
    if (key == E->config->key_undo) {
        if (!E->undo_stack || E->undo_stack->current == 0) {
            editor_set_status(E, "Nothing to undo");
            return 0;
        }

        /* Get action to undo */
        E->undo_stack->current--;
        UndoAction *action = &E->undo_stack->actions[E->undo_stack->current];

        /* Reverse the action */
        if (action->type == UNDO_INSERT) {
            /* Was an insert, so delete it */
            gb_move_gap(E->buffer, action->position);
            for (size_t i = 0; i < action->data_len; i++) {
                gb_delete_at(E->buffer);
            }
            editor_set_cursor_index(E, action->position);
        } else { /* UNDO_DELETE */
            /* Was a delete, so insert it back */
            gb_move_gap(E->buffer, action->position);
            gb_insert_bytes(E->buffer, action->data, action->data_len);
            editor_set_cursor_index(E, action->position + action->data_len);
        }

        E->dirty = 1;
        editor_set_status(E, "Undo");
        return 0;
    }

    /* Redo */
    if (key == E->config->key_redo) {
        if (!E->undo_stack || E->undo_stack->current >= E->undo_stack->count) {
            editor_set_status(E, "Nothing to redo");
            return 0;
        }

        /* Get action to redo */
        UndoAction *action = &E->undo_stack->actions[E->undo_stack->current];
        E->undo_stack->current++;

        /* Redo the action */
        if (action->type == UNDO_INSERT) {
            /* Redo the insert */
            gb_move_gap(E->buffer, action->position);
            gb_insert_bytes(E->buffer, action->data, action->data_len);
            editor_set_cursor_index(E, action->position + action->data_len);
        } else { /* UNDO_DELETE */
            /* Redo the delete */
            gb_move_gap(E->buffer, action->position);
            for (size_t i = 0; i < action->data_len; i++) {
                gb_delete_at(E->buffer);
            }
            editor_set_cursor_index(E, action->position);
        }

        E->dirty = 1;
        editor_set_status(E, "Redo");
        return 0;
    }

    /* Search */
    if (key == E->config->key_search) {
        /* Always start a new search */
        E->search_mode = 1;
        E->search_query[0] = '\0'; /* Clear previous query */
        editor_set_status(E, "Search: ");
        return 0;
    }

    /* Find next match (when in active search) */
    if (key == 'n' && E->search_mode == 2) {
        size_t start = E->last_match_index + 1;
        size_t match_index = search_forward(E->buffer, start, E->search_query);

        if (match_index == (size_t)-1) {
            /* Wrap to beginning */
            match_index = search_forward(E->buffer, 0, E->search_query);

            if (match_index == (size_t)-1 || match_index >= E->last_match_index) {
                editor_set_status(E, "No more matches");
            } else {
                editor_set_cursor_index(E, match_index);
                E->last_match_index = match_index;
                editor_set_status(E, "Wrapped to top (n for next)");
            }
        } else {
            editor_set_cursor_index(E, match_index);
            E->last_match_index = match_index;
            editor_set_status(E, "Found (n for next)");
        }
        return 0;
    }
    /* find previous match */
    if (key == 'N' && E->search_mode == 2) {
        if (E->last_match_index == 0) {
            /* At beginning, wrap to end */
            size_t len = gb_len(E->buffer);
            size_t match_index = search_backward(E->buffer, len, E->search_query);

            if (match_index == (size_t)-1) {
                editor_set_status(E, "No more matches");
            } else {
                editor_set_cursor_index(E, match_index);
                E->last_match_index = match_index;
                editor_set_status(E, "Wrapped to bottom (N for prev, n for next)");
            }
        } else {
            /* Search backward from before current match */
            size_t match_index = search_backward(E->buffer, E->last_match_index - 1, E->search_query);

            if (match_index == (size_t)-1) {
                /* Wrap to end */
                size_t len = gb_len(E->buffer);
                match_index = search_backward(E->buffer, len, E->search_query);

                if (match_index == (size_t)-1 || match_index == E->last_match_index) {
                    editor_set_status(E, "No more matches");
                } else {
                    editor_set_cursor_index(E, match_index);
                    E->last_match_index = match_index;
                    editor_set_status(E, "Wrapped to bottom (N for prev, n for next)");
                }
            } else {
                editor_set_cursor_index(E, match_index);
                E->last_match_index = match_index;
                editor_set_status(E, "Found (N for prev, n for next)");
            }
        }
        return 0;
    }

    if (key != E->config->key_quit)
        E->expect_quit = 0;

    /* Printable characters */
    if (key >= 32 && key <= 126) {
        if (gb_move_gap(E->buffer, E->cursor_index) == 0 && gb_insert_char(E->buffer, (char)key) == 0) {
            /* Record undo action */
            char c = (char)key;
            undo_stack_push(E->undo_stack, UNDO_INSERT, E->cursor_index, &c, 1);
            E->cursor_index += 1;
            E->dirty = 1;
            editor_set_cursor_index(E, E->cursor_index);
        }
        return 0;
    }

    return 0;
}