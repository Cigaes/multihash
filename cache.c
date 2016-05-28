#define _DEFAULT_SOURCE /* for db.h */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <db.h>

#include "cache.h"

struct Stat_cache {
    DB_ENV *db_env;
    DB *db;
};

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

int
stat_cache_alloc(Stat_cache **rcache)
{
    Stat_cache *cache;

    cache = malloc(sizeof(*cache));
    if (cache == NULL) {
        perror("malloc");
        return -1;
    }
    cache->db_env = NULL;
    cache->db = NULL;
    *rcache = cache;
    return 0;
}

void
stat_cache_free(Stat_cache **rcache)
{
    Stat_cache *cache = *rcache;

    if (cache->db != NULL) {
        cache->db->close(cache->db, 0);
        cache->db_env->close(cache->db_env, 0);
    }
    free(cache);
    *rcache = NULL;
}

static int
stat_cache_open(Stat_cache *cache)
{
    int ret;
    char path[2048], *home, *dir_end;

    if (cache->db != NULL)
        return 0;
    home = getenv("HOME");
    if (home == NULL) {
        fprintf(stderr, "$HOME required\n");
        exit(1);
    }
    ret = snprintf(path, sizeof(path), "%s/.cache/multihash/files.db", home);
    if (ret >= (int)sizeof(path)) {
        fprintf(stderr, "$HOME too long\n");
        exit(1);
    }
    create_parent_directory(path, &dir_end);
    /* leaves \0 at the last / in the path */
    ret = db_env_create(&cache->db_env, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to create cache database environment: %s\n",
            db_strerror(ret));
        exit(1);
    }
    ret = cache->db_env->open(cache->db_env, path,
        DB_CREATE | DB_INIT_MPOOL | DB_INIT_LOCK, 0666);
    if (ret != 0) {
        fprintf(stderr, "Failed to open cache database environment: %s\n",
            db_strerror(ret));
        exit(1);
    }
    ret = db_create(&cache->db, cache->db_env, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to create cache database: %s\n",
            db_strerror(ret));
        exit(1);
    }
    *dir_end = '/';
    ret = cache->db->open(cache->db, NULL, path, "file_hash",
        DB_BTREE, DB_CREATE, 0666);
    if (ret != 0) {
        fprintf(stderr, "Failed to open cache database %s: %s\n",
            path, db_strerror(ret));
        exit(1);
    }
    cache->db->sync(cache->db, 0);
    return 0;
}

static void
key_from_filename(DBT *dbt, const char *path, const struct stat *st,
    const char *hash)
{
    unsigned i, l, bufsize = 0;
    char *buf = NULL;

    for (i = 0; i < 2; i++) {
        l = snprintf(buf, bufsize, "%s%c%ju:%ju:%ju.%09u:%s",
            path, 0, (uintmax_t)st->st_size, (uintmax_t)st->st_ino,
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

int
stat_cache_get(Stat_cache *cache, const char *path,
    const struct stat *st, const char *hash,
    uint8_t *data, size_t size)
{
    DBT tkey = { 0 }, tdata = { 0 };
    int ret;

    if (stat_cache_open(cache) < 0)
        return -1;
    key_from_filename(&tkey, path, st, hash);
    tdata.data = data;
    tdata.ulen = size;
    tdata.flags = DB_DBT_USERMEM;
    ret = cache->db->get(cache->db, NULL, &tkey, &tdata, 0);
    free(tkey.data);
    if (ret != 0) {
        if (ret == DB_NOTFOUND)
            return 0;
        fprintf(stderr, "Failed to find in cache: %s\n", db_strerror(ret));
        return -1;
    }
    if (tdata.size != size) {
        fprintf(stderr, "Inconsistent size for %s:%s\n", hash, path);
        return -1;
    }
    return 1;
}

int stat_cache_set(Stat_cache *cache, const char *path,
    const struct stat *st, const char *hash,
    uint8_t *data, size_t size)
{
    DBT tkey = { 0 }, tdata = { 0 };
    int ret;

    if (stat_cache_open(cache) < 0)
        return -1;
    key_from_filename(&tkey, path, st, hash);
    tdata.data = data;
    tdata.size = size;
    ret = cache->db->put(cache->db, NULL, &tkey, &tdata, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to insert in cache: %s\n", db_strerror(ret));
        exit(1);
    }
    free(tkey.data);
    return 0;
}

