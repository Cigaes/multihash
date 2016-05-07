#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "cache.h"
#include "parhash.h"

#define MIN_READ 65536
#define MAX_READ (1024 * 1024)

typedef struct Cahash {
    Parhash *ph;
    Stat_cache *cache;
} Cahash;

static unsigned opt_no_cache = 0;
static unsigned opt_script = 0;
static unsigned opt_verbose = 0;

static unsigned
fill_buffer(Parhash *ph, int fd, unsigned max)
{
    struct iovec iov[2];
    ssize_t r;
    unsigned n;

    n = parhash_get_buffer(ph, max,
        &iov[0].iov_base, &iov[0].iov_len,
        &iov[1].iov_base, &iov[1].iov_len);
    r = readv(fd, iov, n);
    if (r < 0) {
        perror("read");
        exit(1);
    }
    return r;
}

static void
cahash_output(unsigned index, const char *path, Parhash_info *hi)
{
    unsigned i;

    printf("%s:", hi->name);
    for (i = 0; i < hi->size; i++)
        printf("%02x", hi->out[i]);
    if (opt_script)
        printf("  %09d\n", index);
    else
        printf("  %s\n", path);
}

static int
cahash_file_data(Parhash *ph, int fd, struct stat *st)
{
    unsigned rd;

    if (parhash_start(ph) < 0)
        exit(1);
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE);
    while (1) {
        parhash_wait_buffer(ph, MIN_READ);
        /* Do net read too much at once to avoid starving the threads */
        rd = fill_buffer(ph, fd, MAX_READ);
        if (rd == 0)
            break;
        parhash_advance(ph, rd);
    }
    parhash_finish(ph);

    if (fstat(fd, st) < 0) {
        perror("fstat");
        exit(1);
    }
    return 0;
}

static int
cahash_file_data_from_path(Parhash *ph, const char *path, struct stat *st)
{
    int fd, ret;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    ret = cahash_file_data(ph, fd, st);
    close(fd);
    return ret;
}

static int
cahash_file(Cahash *c, unsigned index, const char *path)
{
    Parhash_info *hi;
    char *rpath = NULL;
    struct stat st;
    unsigned i, todo;
    int ret;

    if (opt_no_cache) {
        for (i = 0; (hi = parhash_get_info(c->ph, i)) != NULL; i++)
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
        for (i = 0; (hi = parhash_get_info(c->ph, i)) != NULL; i++) {
            hi->disabled = 0;
            ret = stat_cache_get(c->cache, rpath, &st,
                hi->name, hi->out, hi->size);
            if (ret > 0)
                hi->disabled = 1;
            if (!hi->disabled)
                todo++;
        }
    }
    if (todo) {
        if (cahash_file_data_from_path(c->ph, path, &st)) {
            free(rpath);
            return 1;
        }
    }
    for (i = 0; (hi = parhash_get_info(c->ph, i)) != NULL; i++) {
        cahash_output(index, path, hi);
        if (!opt_no_cache && !hi->disabled)
            stat_cache_set(c->cache, rpath, &st, hi->name, hi->out, hi->size);
    }
    if (opt_verbose) {
        for (i = 0; (hi = parhash_get_info(c->ph, i)) != NULL; i++)
            if (!hi->disabled)
                fprintf(stderr, "%s: %.3fs\n", hi->name,
                    hi->utime_sec + hi->utime_msec / 1E6);
    }
    free(rpath);
    fflush(stdout);
    return 0;
}

static void
usage(int ret)
{
    fprintf(ret == 0 ? stdout : stderr,
        "Usage: cahash [options] files\n");
    exit(ret);
}

int
main(int argc, char **argv)
{
    Cahash c;
    int opt, i, errors = 0;

    while ((opt = getopt(argc, argv, "Csvh")) != -1) {
        switch (opt) {
            case 'C':
                opt_no_cache = 1;
                break;
            case 's':
                opt_script = 1;
                break;
            case 'v':
                opt_verbose = 1;
                break;
            case 'h':
                usage(0);
            default:
                usage(1);
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0)
        usage(1);
    if (parhash_alloc(&c.ph) < 0)
        exit(1);
    if (stat_cache_alloc(&c.cache) < 0)
        exit(1);
    for (i = 0; i < argc; i++)
        errors += cahash_file(&c, i, argv[i]);
    stat_cache_free(&c.cache);
    parhash_free(&c.ph);
    return errors > 0;
}
