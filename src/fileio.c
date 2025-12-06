/*
 * fileio.c
 * Vincent Welbourne
 * vincent.vw04@gmail.com
 *
 * Purpose:
 */
#include "fileio.h"
#include "gapbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int file_save_from_gapbuffer(const char *filename, GapBuffer *g) {
    if (!filename || !g) return -1;
    size_t len = gb_len(g);
    char *buf = malloc(len + 1);
    if (!buf) return -1;
    size_t wrote = gb_export(g, buf, len + 1);
    buf[wrote] = '\0';
    FILE *f = fopen(filename, "wb");
    if (!f) { free(buf); return -1; }
    size_t w = fwrite(buf, 1, wrote, f);
    fclose(f);
    free(buf);
    return (w == wrote) ? 0 : -1;
}

/* check if file exists return 0, if not return -1 */
int file_load_to_gapbuffer(const char *filename, GapBuffer *g) {
	if (!filename || !g) return -1;
	struct stat st;
	if (stat(filename, &st) != 0) return -1;	
	size_t size = st.st_size;
	if (size == 0) return 0;
	FILE *f = fopen(filename, "rb");
	if (!f) return -1;
	char *buf = malloc(size + 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	size_t read = fread(buf, 1, size, f);
	if (read != size) {
		free(buf);
		fclose(f);
		return -1;
	}
	gb_move_gap(g, 0);
	int result = gb_insert_bytes(g, buf, read);
	if (result != 0) {
		free(buf);
		fclose(f);
		return -1;
	}
	free(buf);
	fclose(f);
	return 0;
}
