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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "cache.h"
#include "formatter.h"
#include "parhash.h"
#include "treewalk.h"
#include "archive.h"

#define MIN_READ 65536
#define MAX_READ (1024 * 1024)

typedef struct Multihash {
    Parhash *ph;
    Stat_cache *cache;
    Formatter *formatter;
    const char *rec_root;
    struct Multihash_options {
        uint8_t no_cache;
        uint8_t follow;
        uint8_t recursive;
        uint8_t archive;
        uint8_t script;
        uint8_t verbose;
    } opt;
} Multihash;

typedef struct Stream Stream;
struct Stream {
    unsigned (*fill_buffer)(Stream *, struct iovec *iov, unsigned niov);
};

typedef struct Stream_fd {
    struct Stream stream;
    int fd;
} Stream_fd;

static unsigned stream_fd_fill_buffer(Stream *s,
    struct iovec *iov, unsigned niov)
{
    Stream_fd *s2 = (Stream_fd *)s;
    ssize_t r;

    r = readv(s2->fd, iov, niov);
    if (r < 0) {
        perror("read");
        exit(1);
    }
    return r;
}

static Stream_fd stream_fd(int fd)
{
    return (Stream_fd) {
        .stream.fill_buffer = stream_fd_fill_buffer,
        .fd = fd,
    };
}

static void
multihash_output(Multihash *mh, unsigned index, const char *path)
{
    Parhash_info *hi;
    char buf[512 / 4 + 1];
    unsigned i, j;

    if (mh->formatter) {
        formatter_dict_item(mh->formatter, "hash");
        formatter_dict_open(mh->formatter);
    }
    for (i = 0; (hi = parhash_get_info(mh->ph, i)) != NULL; i++) {
        assert(sizeof(buf) > hi->size * 2);
        for (j = 0; j < hi->size; j++)
            snprintf(buf + j * 2, 3, "%02x", hi->out[j]);
        if (mh->formatter) {
            formatter_dict_item(mh->formatter, hi->name);
            formatter_string(mh->formatter, buf);
        } else {
            printf("%s:%s  ", hi->name, buf);
            if (mh->opt.script)
                printf("%09d\n", index);
            else
                printf("%s\n", path);
        }
    }
    if (mh->formatter) {
        formatter_dict_close(mh->formatter);
    }
}

static void
multihash_stream_data(Parhash *ph, Stream *s)
{
    struct iovec iov[2];
    unsigned n, rd;

    if (parhash_start(ph) < 0)
        exit(1);
    while (1) {
        parhash_wait_buffer(ph, MIN_READ);
        /* Do net read too much at once to avoid starving the threads */
        n = parhash_get_buffer(ph, MAX_READ,
            &iov[0].iov_base, &iov[0].iov_len,
            &iov[1].iov_base, &iov[1].iov_len);
        rd = s->fill_buffer(s, iov, n);
        if (rd == 0)
            break;
        parhash_advance(ph, rd);
    }
    parhash_finish(ph);
}

static int
multihash_file_data(Parhash *ph, int fd, struct stat *st)
{
    Stream_fd s = stream_fd(fd);

    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE);
    multihash_stream_data(ph, &s.stream);
    if (fstat(fd, st) < 0) {
        perror("fstat");
        exit(1);
    }
    return 0;
}

static int
multihash_file_data_from_path(Parhash *ph, const char *path, struct stat *st)
{
    int fd, ret;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    ret = multihash_file_data(ph, fd, st);
    close(fd);
    return ret;
}

static int
multihash_file(Multihash *mh, unsigned index, const char *path, int fd)
{
    Parhash_info *hi;
    char *rpath = NULL;
    struct stat st;
    unsigned i, todo;
    int ret;

    if (mh->opt.no_cache) {
        for (i = 0; (hi = parhash_get_info(mh->ph, i)) != NULL; i++)
            hi->disabled = 0;
        todo = i;
    } else {
        rpath = realpath(path, NULL);
        if (rpath == NULL) {
            perror(path);
            return 1;
        }
        if (stat(rpath, &st) < 0) {
            perror(rpath);
            free(rpath);
            return 1;
        }
        todo = 0;
        for (i = 0; (hi = parhash_get_info(mh->ph, i)) != NULL; i++) {
            hi->disabled = 0;
            ret = stat_cache_get(mh->cache, rpath, &st,
                hi->name, hi->out, hi->size);
            if (ret > 0)
                hi->disabled = 1;
            if (!hi->disabled)
                todo++;
        }
    }
    if (todo) {
        ret = fd < 0 ? multihash_file_data_from_path(mh->ph, path, &st) :
            multihash_file_data(mh->ph, fd, &st);
        if (ret != 0) {
            free(rpath);
            return 1;
        }
    }
    multihash_output(mh, index, path);
    for (i = 0; (hi = parhash_get_info(mh->ph, i)) != NULL; i++) {
        if (!mh->opt.no_cache && !hi->disabled)
            stat_cache_set(mh->cache, rpath, &st, hi->name, hi->out, hi->size);
    }
    if (mh->opt.verbose) {
        for (i = 0; (hi = parhash_get_info(mh->ph, i)) != NULL; i++)
            if (!hi->disabled)
                fprintf(stderr, "%s: %.3fs\n", hi->name,
                    hi->utime_sec + hi->utime_msec / 1E6);
    }
    free(rpath);
    fflush(stdout);
    return 0;
}

static void
multihash_file_stat(Multihash *mh, const char *path, const char *type,
    int size_flag, uint64_t size, const char *target, time_t mtime,
    mode_t mode)
{
    char mode_str[5];

    snprintf(mode_str, sizeof(mode_str), "%04o\n", (int)(mode & 07777));
    formatter_array_item(mh->formatter);
    formatter_dict_open(mh->formatter);
    formatter_dict_item(mh->formatter, "path");
    formatter_string(mh->formatter, path);
    formatter_dict_item(mh->formatter, "type");
    formatter_string(mh->formatter, type);
    if (size_flag) {
        formatter_dict_item(mh->formatter, "size");
        formatter_integer(mh->formatter, size);
    }
    if (target != NULL) {
        formatter_dict_item(mh->formatter, "target");
        formatter_string(mh->formatter, target);
    }
    formatter_dict_item(mh->formatter, "mtime");
    formatter_integer(mh->formatter, mtime);
    formatter_dict_item(mh->formatter, "mode");
    formatter_string(mh->formatter, mode_str);
}

static int
multihash_tree_file(Multihash *mh, Treewalk *tw)
{
    const char *rel_path, *target = NULL;
    const struct stat *st;
    const char *type;
    char *full_path;
    size_t len1, len2;
    int ret = 0, fd;

    rel_path = treewalk_get_path(tw);
    st = treewalk_get_stat(tw);
    fd = treewalk_get_fd(tw);

    type =
        S_ISREG (st->st_mode) ? "F" :
        S_ISDIR (st->st_mode) ? "D" :
        S_ISLNK (st->st_mode) ? "L" :
        S_ISBLK (st->st_mode) ? "b" :
        S_ISCHR (st->st_mode) ? "c" :
        S_ISFIFO(st->st_mode) ? "p" :
        S_ISSOCK(st->st_mode) ? "s" :
        NULL;
    if (type == NULL) {
        fprintf(stderr, "unknown type\n");
        return -1;
    }
    len1 = strlen(mh->rec_root);
    len2 = strlen(rel_path);
    full_path = malloc(len1 + len2 + 1);
    if (full_path == NULL)
        return -1;
    memcpy(full_path, mh->rec_root, len1);
    memcpy(full_path + len1, rel_path, len2 + 1);
    if (S_ISLNK(st->st_mode))
        target = treewalk_readlink(tw);

    multihash_file_stat(mh, rel_path, type, fd >= 0, st->st_size, target,
        st->st_mtime, st->st_mode);
    if (fd >= 0)
        ret = multihash_file(mh, 0, full_path, fd);
    formatter_dict_close(mh->formatter);
    free(full_path);
    return ret == 0 ? 0 : -1;
}

static int
formatted_output_prepare(Multihash *mh)
{
    int ret;

    ret = formatter_alloc(&mh->formatter);
    if (ret < 0)
        return ret;
    formatter_open(mh->formatter);
    formatter_dict_open(mh->formatter);
    formatter_dict_item(mh->formatter, "files");
    formatter_array_open(mh->formatter);
    return 0;
}

static void
formatted_output_finish(Multihash *mh)
{
    formatter_array_close(mh->formatter);
    formatter_dict_close(mh->formatter);
    formatter_close(mh->formatter);
    formatter_free(&mh->formatter);
}

static int
multihash_tree(Multihash *mh)
{
    Treewalk *tw;
    int ret;

    ret = treewalk_open(&tw, mh->rec_root);
    if (ret < 0)
        return 1;
    treewalk_set_follow(tw, mh->opt.follow);
    while (1) {
        ret = multihash_tree_file(mh, tw);
        if (ret < 0)
            break;
        ret = treewalk_next(tw);
        if (ret <= 0)
            break;
    }
    treewalk_free(&tw);
    return ret < 0;
}

typedef struct Stream_archive {
    struct Stream stream;
    Archive_reader *ar;
} Stream_archive;

static unsigned stream_archive_fill_buffer(Stream *s,
    struct iovec *iov, unsigned niov)
{
    Stream_archive *s2 = (Stream_archive *)s;
    int r;

    assert(niov >= 1);
    r = archive_read(s2->ar, iov[0].iov_base, iov[0].iov_len);
    if (r < 0) {
        perror("read");
        exit(1);
    }
    return r;
}

static Stream_archive stream_archive(Archive_reader *ar)
{
    return (Stream_archive) {
        .stream.fill_buffer = stream_archive_fill_buffer,
        .ar = ar,
    };
}

static void
multihash_tar_file(Multihash *mh, Archive_reader *ar)
{
    Stream_archive s = stream_archive(ar);
    char type[2] = { ar->type, 0 };
    char *target;
    int data;

    data = ar->type == 'F';
    target = ar->type == 'L' ? ar->target : NULL;
    assert(data || ar->toread == 0);
    multihash_file_stat(mh, ar->filename, type, data, ar->size, target,
        ar->mtime, ar->mode);
    if (data) {
        multihash_stream_data(mh->ph, &s.stream);
        multihash_output(mh, 0, NULL);
    }
    formatter_dict_close(mh->formatter);
}

static int
multihash_tar(Multihash *mh)
{
    Archive_reader *ar;
    int errors = 0, ret;

    ret = archive_open(&ar, stdin);
    if (ret < 0) {
        perror("archive_open");
        return 1;
    }
    while (1) {
        ret = archive_next(ar);
        if (ret <= 0) {
            if (ret < 0)
                errors = 1;
            break;
        }
        multihash_tar_file(mh, ar);
    }
    archive_free(&ar);
    return errors;
}

static void
usage(int ret)
{
    fprintf(ret == 0 ? stdout : stderr,
        "Usage: multihash [options] files\n"
        "\n"
        "Options:\n"
        "    -C : disable caching\n"
        "    -L : follow symbolic links\n"
        "    -r : process files recursively\n"
        "    -s : script-friendly output\n"
        "    -t : process tar archive from stdin\n"
        "    -v : verbose output\n"
        "    -h : print this help\n"
        "\n"
        "multihash version " VERSION "\n");
    exit(ret);
}

int
main(int argc, char **argv)
{
    Multihash multihash, *mh = &multihash;
    int ret, opt, i, errors = 0;

    mh->formatter = NULL;
    mh->opt.no_cache = 0;
    mh->opt.script = 0;
    mh->opt.verbose = 0;
    while ((opt = getopt(argc, argv, "CLrstvh")) != -1) {
        switch (opt) {
            case 'C':
                mh->opt.no_cache = 1;
                break;
            case 'L':
                mh->opt.follow = 1;
                break;
            case 'r':
                mh->opt.recursive = 1;
                break;
            case 's':
                mh->opt.script = 1;
                break;
            case 't':
                mh->opt.archive = 1;
                break;
            case 'v':
                mh->opt.verbose = 1;
                break;
            case 'h':
                usage(0);
            default:
                usage(1);
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0 && !mh->opt.archive)
        usage(1);
    if (parhash_alloc(&mh->ph) < 0)
        exit(1);
    if (stat_cache_alloc(&mh->cache) < 0)
        exit(1);
    if (mh->opt.recursive) {
        if (argc != 1) {
            fprintf(stderr, "multihash: only one path allowed in "
                "recursive mode\n");
            exit(1);
        }
        ret = formatted_output_prepare(mh);
        if (ret < 0)
            exit(1);
        mh->rec_root = argv[0];
        errors += multihash_tree(mh);
        formatted_output_finish(mh);
    } else if (mh->opt.archive) {
        if (argc != 0) {
            fprintf(stderr, "multihash: will read archive from stdin\n");
            exit(1);
        }
        ret = formatted_output_prepare(mh);
        if (ret < 0)
            exit(1);
        errors += multihash_tar(mh);
        formatted_output_finish(mh);
    } else {
        for (i = 0; i < argc; i++)
            errors += multihash_file(mh, i, argv[i], -1);
    }
    stat_cache_free(&mh->cache);
    parhash_free(&mh->ph);
    return errors > 0;
}
