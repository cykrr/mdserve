#include "frontmatter.h"

#include <string.h>

#define FM_LINE_MAX 1024

/* Recorta espacios y el salto de línea de los dos extremos, in-place. */
static char *trim(char *s)
{
  char *end;

  while (*s == ' ' || *s == '\t') s++;

  end = s + strlen(s);
  while (end > s && (end[-1] == '\n' || end[-1] == '\r' ||
                     end[-1] == ' '  || end[-1] == '\t'))
    end--;
  *end = '\0';

  return s;
}

/* 1 si la línea es sólo el delimitador dado (más espacios y salto). */
static int is_delim(const char *line, const char *delim)
{
  char buf[FM_LINE_MAX];

  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  return strcmp(trim(buf), delim) == 0;
}

/* Interpreta el valor de "publish:". Sólo "true"/"yes"/"1" publican; cualquier
 * otra cosa (incluido un valor vacío o basura) mantiene el archivo privado.
 * El opt-in falla cerrado a propósito: una typo no debe publicar una nota. */
static int truthy(const char *v)
{
  return strcasecmp(v, "true") == 0 ||
         strcasecmp(v, "yes")  == 0 ||
         strcmp(v, "1")        == 0;
}

/* Quita comillas envolventes de un valor YAML, si las tiene. */
static char *unquote(char *v)
{
  size_t n = strlen(v);

  if (n >= 2 && ((v[0] == '"' && v[n - 1] == '"') ||
                 (v[0] == '\'' && v[n - 1] == '\''))) {
    v[n - 1] = '\0';
    return v + 1;
  }
  return v;
}

int fm_scan(FILE *f, struct frontmatter *out)
{
  char line[FM_LINE_MAX];
  long start;

  memset(out, 0, sizeof(*out));

  if (!f) return 0;

  start = ftell(f);
  if (start < 0) return 0;

  if (!fgets(line, sizeof(line), f) || !is_delim(line, "---")) {
    fseek(f, start, SEEK_SET);
    return 0;
  }

  while (fgets(line, sizeof(line), f)) {
    char *colon, *key, *val;

    if (is_delim(line, "---") || is_delim(line, "..."))
      return 1;  /* cierre encontrado: el cursor ya está en el cuerpo */

    if ((colon = strchr(line, ':')) == NULL)
      continue;  /* no es "clave: valor"; se ignora sin romper el parseo */

    *colon = '\0';
    key = trim(line);
    val = unquote(trim(colon + 1));

    if (strcmp(key, "publish") == 0)
      out->publish = truthy(val);
    else if (strcmp(key, "title") == 0)
      snprintf(out->title, sizeof(out->title), "%s", val);
  }

  /* EOF sin cierre: no era frontmatter. Se devuelve el archivo intacto. */
  fseek(f, start, SEEK_SET);
  memset(out, 0, sizeof(*out));
  return 0;
}
