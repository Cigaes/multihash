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

typedef struct Stat_cache Stat_cache;

int stat_cache_alloc(Stat_cache **rcache);

void stat_cache_free(Stat_cache **rcache);

int stat_cache_get(Stat_cache *cache, const char *path,
    const struct stat *st, const char *hash,
    uint8_t *data, size_t size);

int stat_cache_set(Stat_cache *cache, const char *path,
    const struct stat *st, const char *hash,
    uint8_t *data, size_t size);
