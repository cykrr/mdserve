#include <stddef.h>

struct membuffer {
  char* data;
  size_t asize;
  size_t size;
};

void membuf_init(struct membuffer* buf, size_t new_asize);

void membuf_append(struct membuffer* buf, const char* data, size_t size);

void membuf_grow(struct membuffer* buf, size_t new_asize);

void membuf_fini(struct membuffer* buf);

