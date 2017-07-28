typedef struct Archive_reader Archive_reader;

struct Archive_reader {
    FILE *in;
    char *filename;
    uint64_t toread;
    uint64_t size;
    int64_t mtime;
    unsigned mode;
    char *long_filename_buf;
    unsigned long_filename_buf_size;
    uint8_t filename_buf[101];
    uint8_t target[101];
    char type;
};

int archive_open(Archive_reader **rar, FILE *in);

void archive_free(Archive_reader **rar);

int archive_next(Archive_reader *ar);

int archive_read(Archive_reader *ar, uint8_t *buf, int size);
