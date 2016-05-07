typedef struct Stat_cache Stat_cache;

int stat_cache_alloc(Stat_cache **rcache);

void stat_cache_free(Stat_cache **rcache);

int stat_cache_get(Stat_cache *cache, const char *path,
    const struct stat *st, const char *hash,
    uint8_t *data, size_t size);

int stat_cache_set(Stat_cache *cache, const char *path,
    const struct stat *st, const char *hash,
    uint8_t *data, size_t size);
