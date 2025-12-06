/*
 * fileio.h
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose:
 */
#ifndef FILEIO_H
#define FILEIO_H

#include "gapbuffer.h"

int file_save_from_gapbuffer(const char *filename, GapBuffer *g);

int file_load_to_gapbuffer(const char *filename, GapBuffer *g);

#endif
