#include <stdint.h>

typedef struct Formatter Formatter;

int formatter_alloc(Formatter **rfmt);

void formatter_free(Formatter **rfmt);

void formatter_open(Formatter *fmt);

void formatter_close(Formatter *fmt);

void formatter_dict_open(Formatter *fmt);

void formatter_dict_close(Formatter *fmt);

void formatter_dict_item(Formatter *fmt, const char *key);

void formatter_array_open(Formatter *fmt);

void formatter_array_close(Formatter *fmt);

void formatter_array_item(Formatter *fmt);

void formatter_string(Formatter *fmt, const unsigned char *str);

void formatter_integer(Formatter *fmt, intmax_t x);
