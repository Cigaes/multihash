typedef struct Treewalk Treewalk;

int treewalk_open(Treewalk **rtw, const char *path);

void treewalk_free(Treewalk **rtw);

int treewalk_next(Treewalk *tw);

const char *treewalk_get_path(const Treewalk *tw);

const struct stat *treewalk_get_stat(const Treewalk *tw);

int treewalk_get_fd(const Treewalk *tw);
