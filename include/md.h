#include "md4c-html.h"
#include "md4c.h"
#include <stdio.h>

/* Retorna HTML NUL-terminado que el llamador debe free(), o NULL si falla. */
char *md_to_html(FILE *file);
