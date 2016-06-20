/*
 * multihash - compute hashes on collections of files
 * Copyright (c) 2016 Nicolas George <george@nsup.org>
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

#include <stdint.h>

typedef struct Formatter Formatter;

int formatter_alloc(Formatter **rfmt);

void formatter_free(Formatter **rfmt);

void formatter_open(Formatter *fmt);

void formatter_close(Formatter *fmt);

void formatter_dict_open(Formatter *fmt);

void formatter_dict_close(Formatter *fmt);

void formatter_dict_item(Formatter *fmt, const char *key);

void formatter_array_open(Formatter *fmt);

void formatter_array_close(Formatter *fmt);

void formatter_array_item(Formatter *fmt);

void formatter_string(Formatter *fmt, const unsigned char *str);

void formatter_integer(Formatter *fmt, intmax_t x);
