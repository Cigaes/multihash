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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "treewalk.h"

#define PATH_LEN 4095
#define PATH_DEPTH 64

typedef struct Treewalk_file {
    char **files;
    char *all_files;
    int fd;
    unsigned path_len;
    unsigned nb_files;
    unsigned cur_file;
    uint8_t subtree_skipped;
} Treewalk_file;

struct Treewalk {
    Treewalk_file stack[PATH_DEPTH];
    unsigned char path[PATH_LEN + 1];
    struct stat st;
    unsigned depth;
    char target[8192];
    const char **exclude;
    size_t nb_exclude;
    uint8_t opt_follow;
};

static int
read_directory_files(Treewalk_file *file, DIR *dir)
{
    struct dirent de_real, *de;
    char *files = NULL;
    size_t files_alloc = 0, files_used = 0, size;
    unsigned nb_files = 0;
    int ret;

    while (1) {
        ret = readdir_r(dir, &de_real, &de);
        if (ret != 0) {
            fprintf(stderr, "readdir_r failed with ret = %d\n", ret);
            break;
        }
        if (de == NULL)
            break;
        if (de->d_name[0] == '.' && (de->d_name[1] == 0 ||
                (de->d_name[1] == '.' && de->d_name[2] == 0)))
            continue;
        if (nb_files == UINT_MAX) {
            fprintf(stderr, "too many files\n");
            return -1;
        }
        size = strlen(de->d_name) + 1;
        if (size > files_alloc - files_used) {
            while (size > files_alloc - files_used && files_alloc < SIZE_MAX)
                files_alloc |= (files_alloc << 1) | 0xFFF;
            if (size > files_alloc - files_used) {
                fprintf(stderr, "total file names too long\n");
                return -1;
            }
            files = realloc(files, files_alloc);
            if (files == NULL) {
                perror("malloc");
                return -1;
            }
            file->all_files = files;
        }
        memcpy(files + files_used, de->d_name, size);
        files_used += size;
        nb_files++;
    }
    file->nb_files = nb_files;
    return 0;
}

static int compare_char_ptr(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static int
read_directory(Treewalk *tw, Treewalk_file *file)
{
    DIR *dir;
    char *f;
    unsigned i;
    int ret, fd;

    /* closedir() will close it, but we need it for openat() */
    fd = dup(file->fd);
    if (fd < 0) {
        perror("dup");
        return -1;
    }
    dir = fdopendir(fd);
    if (dir == NULL) {
        perror(tw->path);
        return -1;
    }
    ret = read_directory_files(file, dir);
    closedir(dir);
    if (ret < 0)
        return ret;
    file->files = calloc(file->nb_files, sizeof(*file->files));
    if (file->files == NULL)
        return -1;
    f = file->all_files;
    for (i = 0; i < file->nb_files; i++) {
        file->files[i] = f;
        f = strchr(f, 0) + 1;
    }
    qsort(file->files, file->nb_files, sizeof(*file->files), compare_char_ptr);
    return 0;
}

static int
should_recurse(Treewalk *tw)
{
    size_t i;

    if (!S_ISDIR(tw->st.st_mode))
        return 0;
    for (i = 0; i < tw->nb_exclude; i++) {
        if (strcmp(tw->path, tw->exclude[i]) == 0) {
            tw->stack[tw->depth].subtree_skipped = 1;
            return 0;
        }
    }
    return 1;
}

static int
examine_file(Treewalk *tw, int dir, const char *name)
{
    Treewalk_file *file = &tw->stack[tw->depth];
    unsigned flags_stat = 0, flags_open = 0;
    int fd = -1, ret;

    /* stat() before open() in order to avoid opening special files */
    file->fd = -1;
    file->files = NULL;
    file->all_files = NULL;
    file->nb_files = 0;
    file->cur_file = 0;
    file->subtree_skipped = 0;
    if (!tw->opt_follow) {
        flags_stat |= AT_SYMLINK_NOFOLLOW;
        flags_open |= O_NOFOLLOW;
    }
    if (fstatat(dir, name, &tw->st, flags_stat) < 0 &&
        (!tw->opt_follow || errno != ENOENT ||
         fstatat(dir, name, &tw->st, flags_stat | AT_SYMLINK_NOFOLLOW) < 0)) {
        perror(name);
        return -1;
    }
    if (S_ISREG(tw->st.st_mode) || should_recurse(tw)) {
        fd = openat(dir, name, O_RDONLY | flags_open);
        if (fd < 0) {
            perror(name);
            return -1;
        }
        file->fd = fd;
        if (S_ISDIR(tw->st.st_mode)) {
            ret = read_directory(tw, file);
            if (ret < 0)
                return ret;
        }
    }
    if (S_ISLNK(tw->st.st_mode)) {
        ret = readlinkat(dir, name, tw->target, sizeof(tw->target));
        if (ret < 0) {
            perror("readlink");
            return -1;
        }
        if (ret >= (int)sizeof(tw->target)) {
            fprintf(stderr, "symlink target too long\n");
            return -1;
        }
        tw->target[ret] = 0;
    }
    return 0;
}

static int
treewalk_open_real(Treewalk *tw, const char *path)
{
    int ret;

    tw->depth = 0;
    tw->path[0] = '/';
    tw->path[1] = 0;
    tw->stack[0].path_len = 0;
    ret = examine_file(tw, AT_FDCWD, path);
    return ret;
}

int
treewalk_open(Treewalk **rtw, const char *path)
{
    Treewalk *tw;
    int ret;

    tw = malloc(sizeof(*tw));
    if (tw == NULL) {
        perror("malloc");
        return -1;
    }
    tw->opt_follow = 0;
    tw->exclude = NULL;
    tw->nb_exclude = 0;
    ret = treewalk_open_real(tw, path);
    if (ret < 0) {
        free(tw);
        return ret;
    }
    *rtw = tw;
    return 0;
}

void treewalk_free(Treewalk **rtw)
{
    free(*rtw);
    *rtw = NULL;
}

void
treewalk_set_follow(Treewalk *tw, int val)
{
    tw->opt_follow = val;
}

void
treewalk_set_exclude(Treewalk *tw, const char **excl, size_t nb_excl)
{
    tw->exclude = excl;
    tw->nb_exclude = nb_excl;
}

static void
unexamine_file(Treewalk_file *file)
{
    free(file->all_files);
    free(file->files);
    file->all_files = NULL;
    file->files = NULL;
    file->nb_files = 0;
    if (file->fd >= 0)
        close(file->fd);
    file->fd = -1;
}

int
treewalk_next(Treewalk *tw)
{
    Treewalk_file *file = &tw->stack[tw->depth], *child;
    const char *child_name;
    size_t len;
    int ret;

    while (file->cur_file == file->nb_files) {
        unexamine_file(file);
        if (tw->depth == 0)
            return 0;
        tw->depth--;
        file--;
    }
    if (tw->depth == PATH_DEPTH - 1) {
        fprintf(stderr, "Directories too deep\n");
        return -1;
    }
    child_name = file->files[file->cur_file];
    child = &tw->stack[tw->depth + 1];
    len = strlen(child_name) + 1;
    if (len > PATH_LEN - file->path_len) {
        fprintf(stderr, "Path too long\n");
        return -1;
    }
    tw->path[file->path_len] = '/';
    memcpy(tw->path + file->path_len + 1, child_name, len);
    child->path_len = file->path_len + len;
    tw->depth++;
    file->cur_file++;
    ret = examine_file(tw, file->fd, child_name);
    if (ret < 0)
        return ret;
    return 1;
}

const char *
treewalk_get_path(const Treewalk *tw)
{
    return tw->path;
}

const struct stat *
treewalk_get_stat(const Treewalk *tw)
{
    return &tw->st;
}

int
treewalk_get_subtree_skipped(const Treewalk *tw)
{
    return tw->stack[tw->depth].subtree_skipped;
}

int
treewalk_get_fd(const Treewalk *tw)
{
    return S_ISREG(tw->st.st_mode) ? tw->stack[tw->depth].fd : -1;
}

const char *
treewalk_readlink(const Treewalk *tw)
{
    return S_ISLNK(tw->st.st_mode) ? tw->target : NULL;
}
