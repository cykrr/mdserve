#include <stddef.h>

struct membuffer {
  char* data;
  size_t asize; // total
  size_t size; // utilizado
};

/* Crea un membuffer */
void membuf_init(struct membuffer* buf, size_t new_asize);

/* Inserta una cadena en un membuffer */
void membuf_append(struct membuffer* buf, const char* data, size_t size);

/* Aumenta el tamaño de un membuffer */
void membuf_grow(struct membuffer* buf, size_t new_asize);

/* Libera la memoria de un membuffer */
void membuf_fini(struct membuffer* buf);

