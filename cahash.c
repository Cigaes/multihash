#define _GNU_SOURCE /* for RUSAGE_THREAD */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <db.h>

#define MIN_READ 65536

enum Hash_function {
    HASH_CRC32,
    HASH_MD5,
    HASH_SHA1,
    HASH_SHA256,
    HASH_SHA512,
    NB_HASH,
};

typedef union Hash_state {
    unsigned crc32;
    MD5_CTX md5;
    SHA_CTX sha1;
    SHA256_CTX sha256;
    SHA512_CTX sha512;
} Hash_state;

typedef struct Hash_context {
    const char *name;
    unsigned size;
    pthread_t thread;
    unsigned buf_fill;
    unsigned eof;
    unsigned cached;
    uint8_t out[64];
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void (*init)(Hash_state *);
    void (*update)(Hash_state *, const uint8_t *, size_t);
    void (*final)(Hash_state *, uint8_t *, size_t);
} Hash_context;

static uint8_t buffer[4 * 1024 * 1024]; /* power of 2 needed */

static uint32_t crc32_table[256];

static unsigned opt_script = 0;
static unsigned opt_verbose = 0;

static DB_ENV *cache_db_env = NULL;
static DB *cache_db = NULL;

static void
crc32_init(Hash_state *s)
{
    unsigned i, j, c;
    const unsigned base = 0xEDB88320;

    s->crc32 = 0xFFFFFFFF;
    if (crc32_table[1] == 0) {
        for (i = 0; i < 256; i++) {
            c = i;
            for (j = 0; j < 8; j++)
                c = (c >> 1) ^ ((c & 1) ? base : 0);
            crc32_table[i] = c;
        }
    }
}

static void
crc32_update(Hash_state *s, const uint8_t *buf, size_t size)
{
    uint32_t crc = s->crc32;

    while (size-- > 0)
        crc = (crc >> 8) ^ crc32_table[(uint8_t)(crc ^ *(buf++))];
    s->crc32 = crc;
}

static void
crc32_final(Hash_state *s, uint8_t *out, size_t size)
{
    (void)size;
    s->crc32 ^= 0xFFFFFFFF;
    out[0] = s->crc32 >> 24;
    out[1] = s->crc32 >> 16;
    out[2] = s->crc32 >>  8;
    out[3] = s->crc32 >>  0;
}

#define OPENSSL_IMPL(name_lc, name_uc) \
static void \
name_lc ## _init(Hash_state *s) \
{ \
    name_uc ## _Init(&s->name_lc); \
} \
static void \
name_lc ## _update(Hash_state *s, const uint8_t *buf, size_t size) \
{ \
    name_uc ## _Update(&s->name_lc, buf, size); \
} \
static void \
name_lc ## _final(Hash_state *s, uint8_t *out, size_t size) \
{ \
    (void)size; \
    name_uc ## _Final(out, &s->name_lc); \
}

OPENSSL_IMPL(md5, MD5);
OPENSSL_IMPL(sha1, SHA1);
OPENSSL_IMPL(sha256, SHA256);
OPENSSL_IMPL(sha512, SHA512);

static Hash_context context[NB_HASH] = {
    { .name = "crc32",
      .size = 32 / 8,
      .init = crc32_init,
      .update = crc32_update,
      .final = crc32_final,
    },
    { .name = "md5",
      .size = 128 / 8,
      .init = md5_init,
      .update = md5_update,
      .final = md5_final,
    },
    { .name = "sha1",
      .size = 160 / 8,
      .init = sha1_init,
      .update = sha1_update,
      .final = sha1_final,
    },
    { .name = "sha256",
      .size = 256 / 8,
      .init = sha256_init,
      .update = sha256_update,
      .final = sha256_final,
    },
    { .name = "sha512",
      .size = 512 / 8,
      .init = sha512_init,
      .update = sha512_update,
      .final = sha512_final,
    },
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

static void
print_rusage(const char *name)
{
#ifdef RUSAGE_THREAD
    struct rusage u;

    if (!opt_verbose)
        return;
    getrusage(RUSAGE_THREAD, &u);
    fprintf(stderr, "%s: %.3fs\n", name,
        u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1E6);
#endif
}

static void *
cahash_thread(void *ctx_v)
{
    Hash_context *ctx = ctx_v;
    Hash_state state;
    unsigned chunk, pos = 0;

    ctx->init(&state);
    pthread_mutex_lock(&ctx->mutex);
    while (1) {
        chunk = ctx->buf_fill;
        if (chunk == 0) {
            if (ctx->eof)
                break;
            pthread_cond_wait(&ctx->cond, &ctx->mutex);
            continue;
        }
        pthread_mutex_unlock(&ctx->mutex);
        if (chunk > sizeof(buffer) - pos)
            chunk = sizeof(buffer) - pos;
        ctx->update(&state, buffer + pos, chunk);
        pos += chunk;
        pos &= sizeof(buffer) - 1;
        pthread_mutex_lock(&ctx->mutex);
        ctx->buf_fill -= chunk;
        pthread_cond_signal(&ctx->cond);
    }
    pthread_mutex_unlock(&ctx->mutex);
    ctx->final(&state, ctx->out, ctx->size);
    print_rusage(ctx->name);
    return NULL;
}

static unsigned
fill_buffer(int fd, unsigned pos, unsigned size)
{
    struct iovec iov[2];
    ssize_t r;
    unsigned n;

    iov[0].iov_base = buffer + pos;
    if (size <= sizeof(buffer) - pos) {
        iov[0].iov_len = size;
        n = 1;
    } else {
        iov[0].iov_len = sizeof(buffer) - pos;
        iov[1].iov_base = buffer;
        iov[1].iov_len = size - iov[0].iov_len;
        n = 2;
    }
    r = readv(fd, iov, n);
    if (r < 0) {
        perror("read");
        exit(1);
    }
    return r;
}

static void
key_from_filename(DBT *dbt, const char *rpath, const struct stat *st,
    const Hash_context *ctx)
{
    unsigned i, l, bufsize = 0;
    char *buf = NULL;

    for (i = 0; i < 2; i++) {
        l = snprintf(buf, bufsize, "%s%c%ju:%ju:%ju.%09u:%s",
            rpath, 0, (uintmax_t)st->st_size, (uintmax_t)st->st_ino,
            (uintmax_t)st->st_ctim.tv_sec, (unsigned)st->st_ctim.tv_nsec,
            ctx->name);
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
cahash_from_cache(const char *rpath, const struct stat *st, Hash_context *ctx)
{
    DBT tkey = { 0 }, tdata = { 0 };
    int ret;

    cache_open();
    key_from_filename(&tkey, rpath, st, ctx);
    tdata.data = ctx->out;
    tdata.ulen = ctx->size;
    tdata.flags = DB_DBT_USERMEM;
    ret = cache_db->get(cache_db, NULL, &tkey, &tdata, 0);
    free(tkey.data);
    if (ret != 0) {
        if (ret == DB_NOTFOUND)
            return;
        fprintf(stderr, "Failed to find in cache: %s\n", db_strerror(ret));
        exit(1);
    }
    if (tdata.size != ctx->size) {
        fprintf(stderr, "Inconsistent size for %s:%s\n", ctx->name, rpath);
        exit(1);
    }
    ctx->cached = 1;
}

static void
cahash_to_cache(const char *rpath, const struct stat *st, Hash_context *ctx)
{
    DBT tkey = { 0 }, tdata = { 0 };
    int ret;

    cache_open();
    key_from_filename(&tkey, rpath, st, ctx);
    tdata.data = ctx->out;
    tdata.size = ctx->size;
    ret = cache_db->put(cache_db, NULL, &tkey, &tdata, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to insert in cache: %s\n", db_strerror(ret));
        exit(1);
    }
    free(tkey.data);
}

static void
cahash_output(unsigned index, const char *path, Hash_context *ctx)
{
    unsigned i;

    printf("%s:", ctx->name);
    for (i = 0; i < ctx->size; i++)
        printf("%02x", ctx->out[i]);
    if (opt_script)
        printf("  %09d\n", index);
    else
        printf("  %s\n", path);
}

static int
cahash_file_data(const char *path, struct stat *st)
{
    unsigned fill_max, pos = 0, rd, avail = sizeof(buffer);
    unsigned i;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    posix_fadvise(fd, 0, 0, POSIX_FADV_NOREUSE);

    for (i = 0; i < NB_HASH; i++) {
        if (context[i].cached)
            continue;
        context[i].buf_fill = 0;
        context[i].eof = 0;
        if (pthread_create(&context[i].thread, NULL, cahash_thread,
            &context[i]) < 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    while (1) {

        if (avail < MIN_READ) {
            fill_max = 0;
            for (i = 0; i < NB_HASH; i++) {
                if (context[i].cached)
                    continue;
                pthread_mutex_lock(&context[i].mutex);
                while (context[i].buf_fill > sizeof(buffer) - MIN_READ)
                    pthread_cond_wait(&context[i].cond, &context[i].mutex);
                if (context[i].buf_fill > fill_max)
                    fill_max = context[i].buf_fill;
                pthread_mutex_unlock(&context[i].mutex);
            }
            avail = sizeof(buffer) - fill_max;
        }

        /* Do net read too much at once to avoid starving the threads */
        if (avail > sizeof(buffer) / 4)
            avail = sizeof(buffer) / 4;
        rd = fill_buffer(fd, pos, avail);
        if (rd == 0)
            break;
        pos += rd;
        pos &= sizeof(buffer) - 1;
        avail -= rd;

        for (i = 0; i < NB_HASH; i++) {
            if (context[i].cached)
                continue;
            pthread_mutex_lock(&context[i].mutex);
            context[i].buf_fill += rd;
            pthread_mutex_unlock(&context[i].mutex);
            pthread_cond_signal(&context[i].cond);
        }
    }

    if (fstat(fd, st) < 0) {
        perror("fstat");
        exit(1);
    }
    close(fd);
    for (i = 0; i < NB_HASH; i++) {
        if (context[i].cached)
            continue;
        pthread_mutex_lock(&context[i].mutex);
        context[i].eof = 1;
        pthread_mutex_unlock(&context[i].mutex);
        pthread_cond_signal(&context[i].cond);
    }
    for (i = 0; i < NB_HASH; i++) {
        if (context[i].cached)
            continue;
        pthread_join(context[i].thread, NULL);
    }
    return 0;
}

static int
cahash_file(unsigned index, const char *path)
{
    char *rpath;
    struct stat st;
    unsigned i, todo;

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
    for (i = 0; i < NB_HASH; i++) {
        context[i].cached = 0;
        cahash_from_cache(rpath, &st, &context[i]);
        if (!context[i].cached)
            todo++;
    }
    if (todo) {
        if (cahash_file_data(path, &st)) {
            free(rpath);
            return 1;
        }
    }
    for (i = 0; i < NB_HASH; i++) {
        cahash_output(index, path, &context[i]);
        if (!context[i].cached)
            cahash_to_cache(rpath, &st, &context[i]);
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
    int opt, i, errors = 0;

    while ((opt = getopt(argc, argv, "svh")) != -1) {
        switch (opt) {
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
    for (i = 0; i < NB_HASH; i++) {
        pthread_mutex_init(&context[i].mutex, NULL);
        pthread_cond_init(&context[i].cond, NULL);
    }
    for (i = 0; i < argc; i++)
        errors += cahash_file(i, argv[i]);
    if (cache_db != NULL) {
        cache_db->close(cache_db, 0);
        cache_db_env->close(cache_db_env, 0);
    }
    return errors > 0;
}
