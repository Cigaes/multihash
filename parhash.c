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

#define _GNU_SOURCE /* for RUSAGE_THREAD */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/resource.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "parhash.h"

#define BUF_SIZE (4 * 1024 * 1024) /* power of 2 needed */

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
    Parhash_info pub;
    unsigned buf_fill;
    uint8_t eof;
    uint8_t started;
    Parhash *parhash;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void (*init)(Hash_state *);
    void (*update)(Hash_state *, const uint8_t *, size_t);
    void (*final)(Hash_state *, uint8_t *, size_t);
} Hash_context;

struct Parhash {
    Hash_context ctx[NB_HASH];
    unsigned pos;
    unsigned avail;
    uint8_t buf[BUF_SIZE];
};

static uint32_t crc32_table[256];

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

static void
compute_rusage(Hash_context *ctx)
{
#ifdef RUSAGE_THREAD
    struct rusage u;

    getrusage(RUSAGE_THREAD, &u);
    ctx->pub.utime_sec = u.ru_utime.tv_sec;
    ctx->pub.utime_msec = u.ru_utime.tv_usec;
#endif
}

static void *
parhash_thread(void *ctx_v)
{
    Hash_context *ctx = ctx_v;
    Parhash *parhash = ctx->parhash;
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
        if (chunk > sizeof(parhash->buf) - pos)
            chunk = sizeof(parhash->buf) - pos;
        ctx->update(&state, parhash->buf + pos, chunk);
        pos += chunk;
        pos &= sizeof(parhash->buf) - 1;
        pthread_mutex_lock(&ctx->mutex);
        ctx->buf_fill -= chunk;
        pthread_cond_signal(&ctx->cond);
    }
    pthread_mutex_unlock(&ctx->mutex);
    ctx->final(&state, ctx->pub.out, ctx->pub.size);
    compute_rusage(ctx);
    return NULL;
}

int
parhash_alloc(Parhash **rparhash)
{
    Parhash *parhash;
    unsigned i;

    parhash = malloc(sizeof(*parhash));
    if (parhash == NULL) {
        perror("malloc");
        return -1;
    }

    parhash->ctx[HASH_CRC32 ].pub.name = "crc32";
    parhash->ctx[HASH_CRC32 ].pub.size = 32 / 8;
    parhash->ctx[HASH_CRC32 ].init = crc32_init;
    parhash->ctx[HASH_CRC32 ].update = crc32_update;
    parhash->ctx[HASH_CRC32 ].final = crc32_final;

    parhash->ctx[HASH_MD5   ].pub.name = "md5";
    parhash->ctx[HASH_MD5   ].pub.size = 128 / 8;
    parhash->ctx[HASH_MD5   ].init = md5_init;
    parhash->ctx[HASH_MD5   ].update = md5_update;
    parhash->ctx[HASH_MD5   ].final = md5_final;

    parhash->ctx[HASH_SHA1  ].pub.name = "sha1";
    parhash->ctx[HASH_SHA1  ].pub.size = 160 / 8;
    parhash->ctx[HASH_SHA1  ].init = sha1_init;
    parhash->ctx[HASH_SHA1  ].update = sha1_update;
    parhash->ctx[HASH_SHA1  ].final = sha1_final;

    parhash->ctx[HASH_SHA256].pub.name = "sha256";
    parhash->ctx[HASH_SHA256].pub.size = 256 / 8;
    parhash->ctx[HASH_SHA256].init = sha256_init;
    parhash->ctx[HASH_SHA256].update = sha256_update;
    parhash->ctx[HASH_SHA256].final = sha256_final;

    parhash->ctx[HASH_SHA512].pub.name = "sha512";
    parhash->ctx[HASH_SHA512].pub.size = 512 / 8;
    parhash->ctx[HASH_SHA512].init = sha512_init;
    parhash->ctx[HASH_SHA512].update = sha512_update;
    parhash->ctx[HASH_SHA512].final = sha512_final;

    for (i = 0; i < NB_HASH; i++) {
        parhash->ctx[i].parhash = parhash;
        parhash->ctx[i].pub.disabled = 0;
        pthread_mutex_init(&parhash->ctx[i].mutex, NULL);
        pthread_cond_init(&parhash->ctx[i].cond, NULL);
    }

    *rparhash = parhash;
    return 0;
}

void
parhash_free(Parhash **parhash)
{
    free(*parhash);
    *parhash = NULL;
}

Parhash_info *
parhash_get_info(Parhash *parhash, unsigned idx)
{
    return idx < NB_HASH ? &parhash->ctx[idx].pub : NULL;
}

int
parhash_start(Parhash *parhash)
{
    Hash_context *ctx;
    unsigned i;

    parhash->pos = 0;
    parhash->avail = sizeof(parhash->buf);
    for (i = 0; i < NB_HASH; i++) {
        ctx = &parhash->ctx[i];
        ctx->pub.utime_sec = 0;
        ctx->pub.utime_msec = 0;
        ctx->buf_fill = 0;
        ctx->eof = 0;
        ctx->started = 0;
    }
    for (i = 0; i < NB_HASH; i++) {
        ctx = &parhash->ctx[i];
        if (ctx->pub.disabled)
            continue;
        if (pthread_create(&ctx->thread, NULL, parhash_thread, ctx) < 0) {
            perror("pthread_create");
            return -1;
        }
        ctx->started = 1;
    }
    return 0;
}

void
parhash_wait_buffer(Parhash *parhash, size_t min)
{
    Hash_context *ctx;
    unsigned fill_max, i;

    if (parhash->avail < min) {
        fill_max = 0;
        for (i = 0; i < NB_HASH; i++) {
            ctx = &parhash->ctx[i];
            if (ctx->pub.disabled)
                continue;
            pthread_mutex_lock(&ctx->mutex);
            while (ctx->buf_fill > sizeof(parhash->buf) - min)
                pthread_cond_wait(&ctx->cond, &ctx->mutex);
            if (ctx->buf_fill > fill_max)
                fill_max = ctx->buf_fill;
            pthread_mutex_unlock(&ctx->mutex);
        }
        parhash->avail = sizeof(parhash->buf) - fill_max;
    }
}

unsigned
parhash_get_buffer(Parhash *parhash, size_t max,
    void **b1, size_t *s1, void **b2, size_t *s2)
{
    size_t size = parhash->avail < max ? parhash->avail : max;

    *b1 = parhash->buf + parhash->pos;
    if (size <= sizeof(parhash->buf) - parhash->pos) {
        *s1 = size;
        return 1;
    } else {
        *s1 = sizeof(parhash->buf) - parhash->pos;
        *b2 = parhash->buf;
        *s2 = size - *s1;
        return 2;
    }
}

void
parhash_advance(Parhash *parhash, size_t size)
{
    unsigned i;

    parhash->pos += size;
    parhash->pos &= sizeof(parhash->buf) - 1;
    parhash->avail -= size;
    for (i = 0; i < NB_HASH; i++) {
        if (parhash->ctx[i].pub.disabled)
            continue;
        pthread_mutex_lock(&parhash->ctx[i].mutex);
        parhash->ctx[i].buf_fill += size;
        pthread_mutex_unlock(&parhash->ctx[i].mutex);
        pthread_cond_signal(&parhash->ctx[i].cond);
    }
}

void parhash_finish(Parhash *parhash)
{
    unsigned i;

    for (i = 0; i < NB_HASH; i++) {
        if (!parhash->ctx[i].started)
            continue;
        pthread_mutex_lock(&parhash->ctx[i].mutex);
        parhash->ctx[i].eof = 1;
        pthread_mutex_unlock(&parhash->ctx[i].mutex);
        pthread_cond_signal(&parhash->ctx[i].cond);
    }
    for (i = 0; i < NB_HASH; i++) {
        if (parhash->ctx[i].pub.disabled)
            continue;
        pthread_join(parhash->ctx[i].thread, NULL);
    }
}
