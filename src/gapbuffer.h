#ifndef GAPBUFFER_H
#define GAPBUFFER_H

#include <stddef.h>

typedef struct GapBuffer GapBuffer;

/* Create/free */
GapBuffer *gb_new(size_t initial_capacity);
void gb_free(GapBuffer *g);

/* Basic queries */
size_t gb_len(const GapBuffer *g);
size_t gb_capacity(const GapBuffer *g); /* optional */

/* Cursor/gap movement */
int gb_move_gap(GapBuffer *g, size_t index);

/* Insert/delete */
int gb_insert_char(GapBuffer *g, char c);
int gb_insert_bytes(GapBuffer *g, const char *s, size_t n);
int gb_delete_before(GapBuffer *g);
int gb_delete_at(GapBuffer *g);

/* Read/export */
char gb_char_at(const GapBuffer *g, size_t index);
size_t gb_export(const GapBuffer *g, char *out, size_t outcap);

size_t gb_gap_start(const GapBuffer *g);
size_t gb_gap_end(const GapBuffer *g);

/* DEBUGGING */
#ifdef GAPBUFFER_DEBUG
void gb_debug_dump(const GapBuffer *g);
#endif


#endif
