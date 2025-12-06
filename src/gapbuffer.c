#include "gapbuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Gapbuffer object */
struct GapBuffer {
    char *buf;
    size_t bufsize;    /* total buffer capacity */
    size_t gap_start;  /* index of gap start (inclusive) */
    size_t gap_end;    /* index of gap end (exclusive) */
};

/* helper: return number of bytes in tail (after gap) */
static size_t gb_tail_len(const GapBuffer *g) {
    return g->bufsize - g->gap_end;
}

/* helper: current logical length (number of bytes of content) */
size_t gb_len(const GapBuffer *g) {
    if (!g) return 0;
    return g->bufsize - (g->gap_end - g->gap_start);
}

/* Grow the buffer so that at least `min_extra` more gap bytes are available.
   Returns 0 on success, -1 on error (malloc fail). */
static int gb_grow(GapBuffer *g, size_t min_extra) {
    if (!g) return -1;

    /* desired total used capacity (current used + min_extra gap) */
    size_t used = gb_len(g);                 /* bytes of actual content */
    /* compute new buffer size: start with double, ensure it's big enough */
    size_t newsize = g->bufsize ? g->bufsize * 2 : 16;
    while (newsize < used + min_extra + 4) { /* small guard +4 */
        newsize *= 2;
        /* if multiplication would overflow, break */
        if (newsize <= g->bufsize) { newsize = g->bufsize + min_extra + 4; break; }
    }

    /* Allocate */
    char *newbuf = malloc(newsize);
    if (!newbuf) return -1;

    /* Copy head (0 .. gap_start-1) */
    if (g->gap_start > 0) {
        memcpy(newbuf, g->buf, g->gap_start);
    }

    /* Copy tail to the end area so we keep a gap in the middle.
       Compute tail length and new gap_end so tail sits at end of newbuf. */
    size_t tail_len = gb_tail_len(g);
    size_t new_gap_end = newsize - tail_len;
    if (tail_len > 0) {
        memcpy(newbuf + new_gap_end, g->buf + g->gap_end, tail_len);
    }

    /* Free old buffer and update fields */
    free(g->buf);
    g->buf = newbuf;
    g->gap_end = new_gap_end;
    /* gap_start remains the same */
    g->bufsize = newsize;
    return 0;
}

/* Create new gapbuffer with at least initial_capacity bytes total */
GapBuffer *gb_new(size_t initial_capacity) {
    if (initial_capacity < 8) initial_capacity = 8;
    GapBuffer *g = malloc(sizeof(*g));
    if (!g) return NULL;
    g->buf = malloc(initial_capacity);
    if (!g->buf) { free(g); return NULL; }
    g->bufsize = initial_capacity;
    g->gap_start = 0;
    g->gap_end = initial_capacity;
    return g;
}

void gb_free(GapBuffer *g) {
    if (!g) return;
    free(g->buf);
    free(g);
}

/* Return the byte at logical index (0-based). If index >= len, return '\0'.
   Note: this works with byte indexing (not codepoints). */
char gb_char_at(const GapBuffer *g, size_t index) {
    if (!g) return '\0';
    size_t len = gb_len(g);
    if (index >= len) return '\0';
    if (index < g->gap_start) {
        return g->buf[index];
    } else {
        /* map index after gap_start to tail */
        size_t tail_index = index - g->gap_start;
        return g->buf[g->gap_end + tail_index];
    }
}

/* Move the gap so that gap_start == index (index is 0..len).
   Returns 0 on success, -1 on error (index > len or other). */
int gb_move_gap(GapBuffer *g, size_t index) {
    if (!g) return -1;
    size_t len = gb_len(g);
    if (index > len) return -1; /* can't move past EOF */

    if (index == g->gap_start) return 0; /* already there */

    if (index < g->gap_start) {
        /* move gap left: shift bytes [index .. gap_start-1] right to end of gap */
        size_t move_len = g->gap_start - index; /* bytes to move */
        size_t new_gap_end = g->gap_end - move_len;
        /* source: g->buf + index (length move_len)
           dest:   g->buf + new_gap_end
           memmove handles overlap safely */
        memmove(g->buf + new_gap_end, g->buf + index, move_len);
        g->gap_start = index;
        g->gap_end = new_gap_end;
    } else {
        /* move gap right: shift bytes [gap_end .. gap_end + move_len -1] left to gap_start */
        size_t move_len = index - g->gap_start; /* bytes to move from tail to head */
        /* source: g->buf + g->gap_end
           dest:   g->buf + g->gap_start */
        memmove(g->buf + g->gap_start, g->buf + g->gap_end, move_len);
        g->gap_start += move_len;
        g->gap_end += move_len;
    }
    return 0;
}

/* Ensure gap has at least `need` bytes free, grow if necessary. */
static int ensure_gap(GapBuffer *g, size_t need) {
    if (!g) return -1;
    size_t gap = (g->gap_end - g->gap_start);
    if (gap >= need) return 0;
    size_t extra = need - gap;
    return gb_grow(g, extra);
}

/* Insert a single char at the gap (cursor). Returns 0 on success. */
int gb_insert_char(GapBuffer *g, char c) {
    if (!g) return -1;
    if (ensure_gap(g, 1) != 0) return -1;
    g->buf[g->gap_start++] = c;
    return 0;
}

/* Insert n bytes from s at the gap. */
int gb_insert_bytes(GapBuffer *g, const char *s, size_t n) {
    if (!g) return -1;
    if (!s || n == 0) return 0;
    if (ensure_gap(g, n) != 0) return -1;
    memcpy(g->buf + g->gap_start, s, n);
    g->gap_start += n;
    return 0;
}

/* Delete the byte immediately before the gap (backspace). */
int gb_delete_before(GapBuffer *g) {
    if (!g) return -1;
    if (g->gap_start == 0) return -1; /* nothing to delete */
    g->gap_start -= 1;
    return 0;
}

/* Delete the byte immediately after the gap (delete key). */
int gb_delete_at(GapBuffer *g) {
    if (!g) return -1;
    if (g->gap_end >= g->bufsize) return -1; /* nothing after gap */
    g->gap_end += 1;
    return 0;
}

/* Export buffer contents into user-supplied out buffer.
   If out != NULL and outcap > 0, copy up to outcap-1 bytes and NUL-terminate.
   Returns the full logical length (may be greater than outcap-1). */
size_t gb_export(const GapBuffer *g, char *out, size_t outcap) {
    if (!g) return 0;
    size_t len = gb_len(g);

    if (out && outcap > 0) {
        size_t tail = gb_tail_len(g);
        /* copy head */
        size_t head = g->gap_start;
        size_t copy_head = head;
        if (copy_head > outcap - 1) copy_head = outcap - 1;
        if (copy_head > 0) memcpy(out, g->buf, copy_head);

        /* copy tail (maybe partial) */
        size_t remaining = (outcap > 1) ? (outcap - 1 - copy_head) : 0;
        size_t copy_tail = tail;
        if (copy_tail > remaining) copy_tail = remaining;
        if (copy_tail > 0) {
            memcpy(out + copy_head, g->buf + g->gap_end, copy_tail);
            copy_head += copy_tail;
        }

        /* null terminate */
        out[copy_head] = '\0';
    }
    return len;
}

/* Accessors */
size_t gb_gap_start(const GapBuffer *g) { return g ? g->gap_start : 0; }
size_t gb_gap_end  (const GapBuffer *g) { return g ? g->gap_end : 0; }

