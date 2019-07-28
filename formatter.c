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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "formatter.h"

struct Formatter {
    unsigned depth;
    unsigned char has_items;
};

int
formatter_alloc(Formatter **rfmt)
{
    Formatter *fmt;

    fmt = malloc(sizeof(*fmt));
    if (fmt == NULL) {
        perror("malloc");
        return -1;
    }
    *rfmt = fmt;
    return 0;
}

void
formatter_free(Formatter **rfmt)
{
    free(*rfmt);
    *rfmt = NULL;
}

static void
separator(Formatter *fmt, unsigned final)
{
    unsigned i;

    if (fmt->has_items && !final)
        putc(',', stdout);
    putc('\n', stdout);
    for (i = fmt->depth * 3; i > 0; i--)
        putc(' ', stdout);
    fmt->has_items = 1;
}

void
formatter_open(Formatter *fmt)
{
    fmt->depth = 0;
}

int
formatter_close(Formatter *fmt)
{
    assert(fmt->depth == 0);
    putc('\n', stdout);
    fflush(stdout);
    return ferror(stdout);
}

void
formatter_dict_open(Formatter *fmt)
{
    putc('{', stdout);
    fmt->has_items = 0;
    fmt->depth++;
}

void
formatter_dict_close(Formatter *fmt)
{
    fmt->depth--;
    separator(fmt, 1);
    fprintf(stdout, "}");
}

void
formatter_dict_item(Formatter *fmt, const char *key)
{
    separator(fmt, 0);
    formatter_string(fmt, key);
    fputs(" : ", stdout);
}

void
formatter_array_open(Formatter *fmt)
{
    putc('[', stdout);
    fmt->has_items = 0;
    fmt->depth++;
}

void
formatter_array_close(Formatter *fmt)
{
    fmt->depth--;
    separator(fmt, 1);
    fprintf(stdout, "]");
}

void
formatter_array_item(Formatter *fmt)
{
    separator(fmt, 0);
}

void
formatter_string(Formatter *fmt, const unsigned char *str)
{
    static const char plain[] = "\"\\\x08\x0C\x0A\x0D\x09";
    static const char esc[] = "\"\\bfnrt";
    char *match;

    assert(strlen(plain) == strlen(esc));
    (void)fmt;
    putc('"', stdout);
    for (; *str; str++) {
        match = strchr(plain, *str);
        if (match != NULL) {
            putc('\\', stdout);
            putc(esc[match - plain], stdout);
        } else if (*str < 32) {
            fprintf(stdout, "\\u%04x", *str);
        } else {
            putc(*str, stdout);
        }
    }
    putc('"', stdout);
}

void
formatter_integer(Formatter *fmt, intmax_t x)
{
    (void)fmt;
    fprintf(stdout, "%jd", x);
}

void
formatter_bool(Formatter *fmt, int x)
{
    (void)fmt;
    fprintf(stdout, "%s", x ? "true" : "false");
}
