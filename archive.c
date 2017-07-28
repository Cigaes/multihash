#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "archive.h"

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
    *rar = ar;
    return 0;
}

void
archive_free(Archive_reader **rar)
{
    free(*rar);
    *rar = NULL;
}

static intmax_t
get_oct(const uint8_t *p, unsigned len)
{
    uint8_t *end = p + len;
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

#define ATOFF " at offset 0x%jx\n"
#define OFFAT(d) (uintmax_t)(ftello(ar->in) - (d))
#define ATOFFSET(d) ATOFF, OFFAT(d)

int
archive_next(Archive_reader *ar)
{
    uint8_t head[512];
    static const uint8_t magic[8] = "ustar  ";
    int ret, zblocks = 0, len;

    assert(ar->toread == 0);
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
            if (!is_all_zero(head, sizeof(head))) {
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
    if (memcmp(head + 0x101, magic, sizeof(magic)) != 0) {
        fprintf(stderr, "Invalid or unsupported tar file header"
            ATOFF, OFFAT(sizeof(head)));
        return -1;
    }

    memcpy(ar->filename, head + 0x000, 100);
    ar->filename[100] = 0;
    memcpy(ar->target, head + 0x09d, 100);
    ar->target[100] = 0;
    ar->mode = get_oct(head + 0x064, 8);
    ar->size = get_oct(head + 0x07c, 12);
    ar->mtime = get_oct(head + 0x088, 12);
    len = strlen(ar->filename);
    if (len > 0 && ar->filename[len - 1] == '/')
        ar->filename[--len] = 0;
    switch (head[0x09c]) {
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
                head[0x09c], OFFAT(sizeof(head)));
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
