#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "archive.h"

#define OFF_PATH        0x000
#define OFF_MODE        0x064
#define OFF_SIZE        0x07c
#define OFF_MTIME       0x088
#define OFF_TYPE        0x09c
#define OFF_TARGET      0x09d
#define OFF_MAGIC       0x101
#define LEN_PATH        100

int
archive_open(Archive_reader **rar, FILE *in)
{
    Archive_reader *ar;

    ar = malloc(sizeof(*ar));
    if (ar == NULL) {
        perror("malloc");
        return -1;
    }
    ar->in = in;
    ar->toread = 0;
    ar->long_filename_buf = NULL;
    ar->long_filename_buf_size = 0;
    *rar = ar;
    return 0;
}

void
archive_free(Archive_reader **rar)
{
    free((*rar)->long_filename_buf);
    free(*rar);
    *rar = NULL;
}

static intmax_t
get_oct(const uint8_t *p, unsigned len)
{
    const uint8_t *end = p + len;
    intmax_t r = 0;

    while (p < end && *p == ' ')
        p++;
    while (p < end && (unsigned)(*p - '0') < 8)
        r = (r << 3) | (*(p++) - '0');
    return r;
}

static int
is_all_zero(uint8_t *data, size_t size)
{
    while (size-- > 0)
        if (*(data++) != 0)
            return 0;
    return 1;
}

static unsigned
get_mode(const uint8_t *head)
{
    return get_oct(head + OFF_MODE, 8);
}

static uint64_t
get_size(const uint8_t *head)
{
    return get_oct(head + OFF_SIZE, 12);
}

static uint64_t
get_mtime(const uint8_t *head)
{
    return get_oct(head + OFF_MTIME, 12);
}

#define ATOFF " at offset 0x%jx\n"
#define OFFAT(d) (uintmax_t)(ftello(ar->in) - (d))
#define ATOFFSET(d) ATOFF, OFFAT(d)

static int
read_long_something(Archive_reader *ar, uint8_t *head,
    char **val, char **buf, unsigned *buf_size)
{
    static const uint8_t magic[LEN_PATH] = "././@LongLink";
    uint64_t size, bsize;
    int ret;

    if (memcmp(head + OFF_PATH, magic, sizeof(magic)) != 0) {
        fprintf(stderr, "Invalid long entry pseudo-path"
            ATOFFSET(sizeof(head)));
        return -1;
    }
    size = get_size(head);
    if (size >= 65536) {
        fprintf(stderr, "Long file name really too long"
            ATOFFSET(sizeof(head)));
        return -1;
    }
    bsize = (size + 0x1FF) &~ 0x1FF;
    if (bsize > *buf_size) {
        char *n = realloc(*buf, bsize + 1);
        if (n == NULL) {
            perror("malloc");
            return -1;
        }
        *buf = n;
        *buf_size = bsize;
    }
    ret = fread(*buf, 1, bsize, ar->in);
    if (ret != (int)bsize) {
        if (ret < 0)
            fprintf(stderr, "Read error in tar file" ATOFFSET(0));
        else
            fprintf(stderr, "Truncated tar file" ATOFFSET(0));
        return -1;
    }
    (*buf)[size] = 0;
    (*val) = *buf;
    return 0;
}

static int
read_long_file_name(Archive_reader *ar, uint8_t *head)
{
    return read_long_something(ar, head, &ar->filename,
        &ar->long_filename_buf, &ar->long_filename_buf_size);
}

static int
read_long_link(Archive_reader *ar, uint8_t *head)
{
    return read_long_something(ar, head, &ar->target,
        &ar->long_target_buf, &ar->long_target_buf_size);
}

int
archive_next(Archive_reader *ar)
{
    uint8_t head[512];
    static const uint8_t magic[8] = "ustar  ";
    int ret, zblocks = 0, type, len;

    assert(ar->toread == 0);
    ar->filename = ar->filename_buf;
    ar->target = ar->target_buf;
    while (1) {
        ret = fread(head, 1, sizeof(head), ar->in);
        if (ret == 0) {
            if (ferror(ar->in) || !feof(ar->in)) {
                fprintf(stderr, "Read error in tar file" ATOFFSET(0));
                return -1;
            }
            return 0;
        } else if (ret < (int)sizeof(head)) {
            fprintf(stderr, "Truncated tar file" ATOFFSET(0));
            return -1;
        } else {
            type = head[OFF_TYPE];
            if (type == 'L') {
                ret = read_long_file_name(ar, head);
                if (ret < 0)
                    return -1;
            } else if (type == 'K') {
                ret = read_long_link(ar, head);
                if (ret < 0)
                    return -1;
            } else if (!is_all_zero(head, sizeof(head))) {
                break;
            } else {
                zblocks++;
            }
        }
    }
    if (zblocks == 1) {
        fprintf(stderr, "Strange zero blocks" ATOFFSET(zblocks * sizeof(head)));
        return -1;
    }
    if (memcmp(head + OFF_MAGIC, magic, sizeof(magic)) != 0) {
        fprintf(stderr, "Invalid or unsupported tar file header"
            ATOFFSET(sizeof(head)));
        return -1;
    }

    memcpy(ar->filename_buf, head + OFF_PATH, LEN_PATH);
    ar->filename_buf[LEN_PATH] = 0;
    memcpy(ar->target_buf, head + OFF_TARGET, LEN_PATH);
    ar->target_buf[LEN_PATH] = 0;
    ar->mode = get_mode(head);
    ar->size = get_size(head);
    ar->mtime = get_mtime(head);
    len = strlen(ar->filename_buf);
    if (len > 0 && ar->filename_buf[len - 1] == '/')
        ar->filename_buf[--len] = 0;
    switch (type) {
        case 0:
        case '0':
        case '7': ar->type = 'F'; break;
        case '2': ar->type = 'L'; break;
        case '3': ar->type = 'c'; break;
        case '4': ar->type = 'b'; break;
        case '5': ar->type = 'D'; break;
        case '6': ar->type = 'p'; break;
        case '1':
            fprintf(stderr, "Hard links not supported" ATOFFSET(0));
            return -1;
        default:
            fprintf(stderr, "Unsupported file type '%c'" ATOFF,
                type, OFFAT(sizeof(head)));
            return -1;
    }
    ar->toread = ar->size;
    if (ar->type != 'F' && ar->toread != 0) {
        fprintf(stderr, "Special file with size\n");
        return -1;
    }

    return 1;
}

int
archive_read(Archive_reader *ar, uint8_t *buf, int size)
{
    unsigned pad;
    uint8_t padbuf[512];
    int ret;

    if (ar->toread == 0)
        return 0;
    if ((uint64_t)size > ar->toread)
        size = ar->toread;
    ret = fread(buf, 1, size, ar->in);
    /* TODO read errors */
    if (ret < size)
        return -1;
    ar->toread -= size;
    if (ar->toread == 0) {
        pad = 512 - (ar->size & 511);
        if (pad < 512) {
            ret = fread(padbuf, 1, pad, ar->in);
            if (ret < (int)pad)
                return -1;
        }
    }
    return size;
}

#if 0

int
main(void)
{
    Archive_reader *ar;
    int ret;

    if (archive_open(&ar, stdin) < 0)
        exit(1);
    while (1) {
        printf("\n");
        ret = archive_next(ar);
        printf("ret = %d\n", ret);
        if (ret <= 0)
            break;
        printf("file: '%s'\n", ar->filename);
        printf("type: %c\n", ar->type);
        printf("mode: %o\n", ar->mode);
        printf("size: %d\n", (int)ar->size);
        if (ar->type == 'F') {
            uint8_t buf[100];
            while (1) {
                ret = archive_read(ar, buf, sizeof(buf));
                if (ret < 0)
                    exit(2);
                if (ret == 0)
                    break;
                printf("read %d\n", ret);
                //fwrite(buf, 1, ret, stdout);
            }
        }
        printf("\n");
    }
    archive_free(&ar);
    return 0;
}

#endif
