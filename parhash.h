typedef struct Parhash_info {
    const char *name;
    uint64_t utime_sec, utime_msec;
    uint8_t size;
    uint8_t disabled;
    uint8_t out[64];
} Parhash_info;

typedef struct Parhash Parhash;

int parhash_alloc(Parhash **rparhash);

void parhash_free(Parhash **parhash);

Parhash_info *parhash_get_info(Parhash *parhash, unsigned idx);

int parhash_start(Parhash *parhash);

void parhash_wait_buffer(Parhash *parhash, size_t min);

unsigned parhash_get_buffer(Parhash *parhash, size_t max,
    void **b1, size_t *s1, void **b2, size_t *s2);

void parhash_advance(Parhash *parhash, size_t size);

void parhash_finish(Parhash *parhash);
