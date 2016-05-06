#define _DEFAULT_SOURCE /* for db.h */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <db.h>

#include "parhash.h"

#define MIN_READ 65536
#define MAX_READ (1024 * 1024)

static unsigned opt_no_cache = 0;
static unsigned opt_script = 0;
static unsigned opt_verbose = 0;

static DB_ENV *cache_db_env = NULL;
static DB *cache_db = NULL;

static void
create_parent_directory(char *path, char **end)
{
    char *sep = path, *p;

    assert(*sep == '/');
    for (p = path + 1; *p != 0; p++) {
        if (*p == '/') {
            *sep = '/';
            sep = p;
            *(p++) = 0;
            if (mkdir(path, 0700) < 0 && errno != EEXIST) {
                perror(path);
                exit(1);
            }
        }
    }
    *end = sep;
}

static void
cache_open(void)
{
    int ret;
    char path[2048], *home, *dir_end;

    if (cache_db != NULL)
        return;
    home = getenv("HOME");
    if (home == NULL) {
        fprintf(stderr, "$HOME required\n");
        exit(1);
    }
    ret = snprintf(path, sizeof(path), "%s/.cache/cahash/files.db", home);
    if (ret >= (int)sizeof(path)) {
        fprintf(stderr, "$HOME too long\n");
        exit(1);
    }
    create_parent_directory(path, &dir_end);
    /* leaves \0 at the last / in the path */
    ret = db_env_create(&cache_db_env, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to create cache database environment: %s\n",
            db_strerror(ret));
        exit(1);
    }
    ret = cache_db_env->open(cache_db_env, path,
        DB_CREATE | DB_INIT_MPOOL | DB_INIT_LOCK, 0666);
    if (ret != 0) {
        fprintf(stderr, "Failed to open cache database environment: %s\n",
            db_strerror(ret));
        exit(1);
    }
    ret = db_create(&cache_db, cache_db_env, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to create cache database: %s\n",
            db_strerror(ret));
        exit(1);
    }
    *dir_end = '/';
    ret = cache_db->open(cache_db, NULL, path, "file_hash",
        DB_BTREE, DB_CREATE, 0666);
    if (ret != 0) {
        fprintf(stderr, "Failed to open cache database %s: %s\n",
            path, db_strerror(ret));
        exit(1);
    }
    cache_db->sync(cache_db, 0);
}

static unsigned
fill_buffer(Parhash *parhash, int fd, unsigned max)
{
    struct iovec iov[2];
    ssize_t r;
    unsigned n;

    n = parhash_get_buffer(parhash, max,
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
key_from_filename(DBT *dbt, const char *rpath, const struct stat *st,
    const char *hash)
{
    unsigned i, l, bufsize = 0;
    char *buf = NULL;

    for (i = 0; i < 2; i++) {
        l = snprintf(buf, bufsize, "%s%c%ju:%ju:%ju.%09u:%s",
            rpath, 0, (uintmax_t)st->st_size, (uintmax_t)st->st_ino,
            (uintmax_t)st->st_ctim.tv_sec, (unsigned)st->st_ctim.tv_nsec,
            hash);
        if (l <= 0) {
            perror("snprintf");
            exit(1);
        }
        if (buf == NULL) {
            bufsize = l + 1;
            buf = malloc(bufsize);
            if (buf == NULL) {
                perror("malloc");
                exit(1);
            }
        }
    }
    dbt->data = buf;
    dbt->size = bufsize - 1;
}

static void
cahash_from_cache(const char *rpath, const struct stat *st, Parhash_info *hi)
{
    DBT tkey = { 0 }, tdata = { 0 };
    int ret;

    cache_open();
    key_from_filename(&tkey, rpath, st, hi->name);
    tdata.data = hi->out;
    tdata.ulen = hi->size;
    tdata.flags = DB_DBT_USERMEM;
    ret = cache_db->get(cache_db, NULL, &tkey, &tdata, 0);
    free(tkey.data);
    if (ret != 0) {
        if (ret == DB_NOTFOUND)
            return;
        fprintf(stderr, "Failed to find in cache: %s\n", db_strerror(ret));
        exit(1);
    }
    if (tdata.size != hi->size) {
        fprintf(stderr, "Inconsistent size for %s:%s\n", hi->name, rpath);
        exit(1);
    }
    hi->disabled = 1;
}

static void
cahash_to_cache(const char *rpath, const struct stat *st, Parhash_info *hi)
{
    DBT tkey = { 0 }, tdata = { 0 };
    int ret;

    cache_open();
    key_from_filename(&tkey, rpath, st, hi->name);
    tdata.data = hi->out;
    tdata.size = hi->size;
    ret = cache_db->put(cache_db, NULL, &tkey, &tdata, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to insert in cache: %s\n", db_strerror(ret));
        exit(1);
    }
    free(tkey.data);
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
cahash_file_data(Parhash *parhash, int fd, struct stat *st)
{
    unsigned rd;

    if (parhash_start(parhash) < 0)
        exit(1);
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE);
    while (1) {
        parhash_wait_buffer(parhash, MIN_READ);
        /* Do net read too much at once to avoid starving the threads */
        rd = fill_buffer(parhash, fd, MAX_READ);
        if (rd == 0)
            break;
        parhash_advance(parhash, rd);
    }
    parhash_finish(parhash);

    if (fstat(fd, st) < 0) {
        perror("fstat");
        exit(1);
    }
    return 0;
}

static int
cahash_file_data_from_path(Parhash *parhash, const char *path, struct stat *st)
{
    int fd, ret;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    ret = cahash_file_data(parhash, fd, st);
    close(fd);
    return ret;
}

static int
cahash_file(Parhash *parhash, unsigned index, const char *path)
{
    Parhash_info *hi;
    char *rpath = NULL;
    struct stat st;
    unsigned i, todo;

    if (opt_no_cache) {
        for (i = 0; (hi = parhash_get_info(parhash, i)) != NULL; i++)
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
        for (i = 0; (hi = parhash_get_info(parhash, i)) != NULL; i++) {
            hi->disabled = 0;
            cahash_from_cache(rpath, &st, hi);
            if (!hi->disabled)
                todo++;
        }
    }
    if (todo) {
        if (cahash_file_data_from_path(parhash, path, &st)) {
            free(rpath);
            return 1;
        }
    }
    for (i = 0; (hi = parhash_get_info(parhash, i)) != NULL; i++) {
        cahash_output(index, path, hi);
        if (!opt_no_cache && !hi->disabled)
            cahash_to_cache(rpath, &st, hi);
    }
    if (opt_verbose) {
        for (i = 0; (hi = parhash_get_info(parhash, i)) != NULL; i++)
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
    Parhash *parhash;
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
    if (parhash_alloc(&parhash) < 0)
        exit(1);
    for (i = 0; i < argc; i++)
        errors += cahash_file(parhash, i, argv[i]);
    if (cache_db != NULL) {
        cache_db->close(cache_db, 0);
        cache_db_env->close(cache_db_env, 0);
    }
    parhash_free(&parhash);
    return errors > 0;
}
