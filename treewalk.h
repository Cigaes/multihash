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

typedef struct Treewalk Treewalk;

int treewalk_open(Treewalk **rtw, const char *path);

void treewalk_free(Treewalk **rtw);

void treewalk_set_follow(Treewalk *tw, int val);

void treewalk_set_exclude(Treewalk *tw, const char **excl, size_t nb_excl);

int treewalk_next(Treewalk *tw);

const char *treewalk_get_path(const Treewalk *tw);

const struct stat *treewalk_get_stat(const Treewalk *tw);

int treewalk_get_fd(const Treewalk *tw);

const char *treewalk_readlink(const Treewalk *tw);

int treewalk_get_subtree_skipped(const Treewalk *tw);
