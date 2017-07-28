/*
 * multihash - compute hashes on collections of files
 * Copyright (c) 2017 Nicolas George <george@nsup.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */

typedef struct Archive_reader Archive_reader;

struct Archive_reader {
    FILE *in;
    char *filename;
    char *target;
    uint64_t toread;
    uint64_t size;
    int64_t mtime;
    unsigned mode;
    char *long_filename_buf;
    char *long_target_buf;
    unsigned long_filename_buf_size;
    unsigned long_target_buf_size;
    uint8_t filename_buf[101];
    uint8_t target_buf[101];
    char type;
};

int archive_open(Archive_reader **rar, FILE *in);

void archive_free(Archive_reader **rar);

int archive_next(Archive_reader *ar);

int archive_read(Archive_reader *ar, uint8_t *buf, int size);
