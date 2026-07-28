#ifndef FRONTMATTER_H
#define FRONTMATTER_H

#include <stdio.h>

#define FM_TITLE_MAX 256

struct frontmatter {
  int  publish;                 /* 1 si "publish: true" está presente */
  char title[FM_TITLE_MAX];     /* "" si no se declaró */
};

/* Consume un bloque de frontmatter al inicio del archivo.
 *
 * Un bloque es "---" en su propia línea, cero o más líneas "clave: valor", y un
 * "---" o "..." de cierre. Si el archivo no empieza con "---", o si nunca
 * aparece el cierre, se rebobina a la posición inicial y se retorna 0: un
 * documento que empieza con un thematic break no es frontmatter roto.
 *
 * En caso de éxito el cursor del FILE* queda en el primer byte del cuerpo, que
 * es exactamente lo que md_to_html() va a leer.
 *
 * Retorna 1 si consumió un bloque, 0 si no había. *out siempre queda
 * inicializado. */
int fm_scan(FILE *f, struct frontmatter *out);

#endif /* FRONTMATTER_H */
