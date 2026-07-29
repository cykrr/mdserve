/* mdbuild — generador de sitio estático para el mismo árbol de notas que sirve
 * mdserve.
 *
 * Comparte el núcleo de render (src/md.c, src/membuf.c, md4c vendorizado) pero
 * no enlaza mongoose ni OpenSSL: acá no hay HTTP, sólo archivos.
 *
 * Modelo de URLs: "pretty dirs". md/notes/rust.md se emite en
 * dist/notes/rust/index.html y se sirve como /notes/rust/. Eso mueve cada
 * página un nivel más abajo que su fuente, así que los links relativos del
 * markdown se reescriben a rutas absolutas desde la raíz del sitio (ver
 * rewrite_links). El sitio queda amarrado a la raíz del dominio.
 *
 * Publicación: opt-in. Un .md sólo se emite si su frontmatter dice
 * "publish: true". Los archivos que no son markdown (imágenes, adjuntos) se
 * copian siempre, porque si no las notas publicadas quedan con links rotos.
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "frontmatter.h"
#include "md.h"
#include "membuf.h"

#define PATH_MAX_LEN 4096
#define NAME_MAX_LEN 256

static const char *g_head = "head.html";
static const char *g_tail = "tail.html";
static const char *g_404  = "404.html";

static int g_pages   = 0;
static int g_assets  = 0;
static int g_skipped = 0;

/* ------------------------------------------------------------------ paths */

/* mkdir -p. Retorna 0 en éxito. */
static int mkdir_p(const char *path)
{
  char tmp[PATH_MAX_LEN];
  char *p;

  snprintf(tmp, sizeof(tmp), "%s", path);

  for (p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;

  return 0;
}

/* Normaliza una ruta URL absoluta resolviendo "." y "..".
 *
 * Trabaja sobre la ruta ya concatenada (urldir + link relativo), así que un
 * "../" de más no puede salir del sitio: los pop de más se descartan contra la
 * raíz. Preserva el "/" final si lo había. */
static void normalize_url(const char *in, char *out, size_t out_len)
{
  const char *seg = in;
  char stack[64][NAME_MAX_LEN];
  int  depth = 0;
  int  trailing = 0;
  size_t used = 1;
  int  i;

  if (*in) trailing = in[strlen(in) - 1] == '/';

  while (*seg == '/') seg++;

  while (*seg) {
    const char *end = strchr(seg, '/');
    size_t len = end ? (size_t)(end - seg) : strlen(seg);

    if (len == 0 || (len == 1 && seg[0] == '.')) {
      /* nada que hacer */
    } else if (len == 2 && seg[0] == '.' && seg[1] == '.') {
      if (depth > 0) depth--;
    } else if (depth < 64 && len < NAME_MAX_LEN) {
      memcpy(stack[depth], seg, len);
      stack[depth][len] = '\0';
      depth++;
    }

    if (!end) break;
    seg = end + 1;
  }

  out[0] = '/';
  out[1] = '\0';

  for (i = 0; i < depth; i++) {
    size_t len = strlen(stack[i]);
    if (used + len + 2 >= out_len) break;
    memcpy(out + used, stack[i], len);
    used += len;
    if (i < depth - 1 || trailing) out[used++] = '/';
    out[used] = '\0';
  }
}

/* ------------------------------------------------------------------ escape */

/* membuf_append() para literales, sin contar los bytes a mano. */
static void put(struct membuffer *buf, const char *s)
{
  membuf_append(buf, s, strlen(s));
}

static void html_esc(struct membuffer *buf, const char *s)
{
  for (; *s; s++) {
    switch (*s) {
      case '<':  membuf_append(buf, "&lt;",   4); break;
      case '>':  membuf_append(buf, "&gt;",   4); break;
      case '&':  membuf_append(buf, "&amp;",  5); break;
      case '"':  membuf_append(buf, "&quot;", 6); break;
      case '\'': membuf_append(buf, "&#39;",  5); break;
      default:   membuf_append(buf, s, 1);        break;
    }
  }
}

/* Date metadata is emitted outside the Markdown body so it cannot be confused
 * with authored content. Escape it for both the visible text and datetime. */
static void put_date(struct membuffer *buf, const struct frontmatter *fm)
{
  if (!fm->date[0]) return;

  put(buf, "<p class=\"date\"><time datetime=\"");
  html_esc(buf, fm->date);
  put(buf, "\">");
  html_esc(buf, fm->date);
  put(buf, "</time></p>\n");
}

/* Percent-encode para el href de un listado. Deja pasar los caracteres que son
 * seguros dentro de un path. */
static void url_enc(struct membuffer *buf, const char *s)
{
  static const char hex[] = "0123456789ABCDEF";
  const unsigned char *p = (const unsigned char *) s;

  for (; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
        (*p >= '0' && *p <= '9') || strchr("-_.~/", *p)) {
      membuf_append(buf, (const char *) p, 1);
    } else {
      char esc[3] = { '%', hex[*p >> 4], hex[*p & 15] };
      membuf_append(buf, esc, 3);
    }
  }
}

/* ------------------------------------------------------- reescritura links */

/* 1 si la URL no debe tocarse: absoluta, externa, o puro fragmento. */
static int url_is_fixed(const char *u)
{
  if (u[0] == '\0' || u[0] == '#' || u[0] == '/') return 1;
  if (strstr(u, "://")) return 1;
  if (strncasecmp(u, "mailto:", 7) == 0) return 1;
  if (strncasecmp(u, "tel:", 4) == 0) return 1;
  if (strncasecmp(u, "data:", 5) == 0) return 1;
  return 0;
}

/* Convierte un link relativo del markdown en una ruta absoluta del sitio.
 *
 * urldir es el directorio URL del documento *fuente* (p.ej. "/notes/"), no el
 * de la página emitida. Esa distinción es el punto entero de la función: la
 * página vive en /notes/rust/, un nivel más abajo, así que resolver relativo a
 * ella daría /notes/rust/other/ para un simple "other.md". */
static void map_link(const char *url, const char *urldir,
                     char *out, size_t out_len)
{
  char work[PATH_MAX_LEN];
  /* urldir y work son ambos PATH_MAX_LEN; el destino se dimensiona para la
   * concatenación completa más el "/" y el NUL, así el compilador puede
   * descartar la truncación en vez de advertirla. normalize_url() después
   * recorta a out_len. */
  char joined[2 * PATH_MAX_LEN + 2];
  char *suffix;
  size_t len;

  snprintf(work, sizeof(work), "%s", url);

  /* El "#anchor" y el "?query" se guardan aparte y se re-pegan al final. */
  suffix = work + strcspn(work, "#?");
  {
    char keep[PATH_MAX_LEN];
    snprintf(keep, sizeof(keep), "%s", suffix);
    *suffix = '\0';

    len = strlen(work);
    if (len > 3 && strcmp(work + len - 3, ".md") == 0) {
      work[len - 3] = '\0';
      /* "dir/index.md" apunta al directorio mismo, no a "dir/index/". */
      len -= 3;
      if (len >= 5 && strcmp(work + len - 5, "index") == 0 &&
          (len == 5 || work[len - 6] == '/'))
        work[len - 5] = '\0';
      snprintf(joined, sizeof(joined), "%s%s/", urldir, work);
    } else {
      snprintf(joined, sizeof(joined), "%s%s", urldir, work);
    }

    normalize_url(joined, out, out_len);

    /* Re-pegar el "#anchor"/"?query" que se apartó al inicio. */
    {
      size_t have = strlen(out);
      size_t want = strlen(keep);
      if (have + want < out_len) memcpy(out + have, keep, want + 1);
    }
  }
}

/* Reescribe los href/src relativos del HTML renderizado a rutas absolutas.
 *
 * Opera sobre el texto ya escapado por md4c y sólo hace cirugía de path, así
 * que no hace falta re-escapar: las entidades que hubiera se copian tal cual. */
static struct membuffer rewrite_links(const char *html, const char *urldir)
{
  struct membuffer out = {0};
  const char *p = html;

  membuf_init(&out, strlen(html) + strlen(html) / 4 + 64);

  while (*p) {
    const char *attr = NULL;
    size_t attr_len = 0;
    const char *h = strstr(p, "href=\"");
    const char *s = strstr(p, "src=\"");

    if (h && (!s || h < s))      { attr = h; attr_len = 6; }
    else if (s)                  { attr = s; attr_len = 5; }
    else break;

    membuf_append(&out, p, (size_t)(attr - p) + attr_len);
    p = attr + attr_len;

    {
      const char *close = strchr(p, '"');
      char url[PATH_MAX_LEN];
      size_t ulen;

      if (!close) break;

      ulen = (size_t)(close - p);
      if (ulen >= sizeof(url)) {
        membuf_append(&out, p, ulen);
      } else {
        memcpy(url, p, ulen);
        url[ulen] = '\0';

        if (url_is_fixed(url)) {
          membuf_append(&out, url, ulen);
        } else {
          char mapped[PATH_MAX_LEN];
          map_link(url, urldir, mapped, sizeof(mapped));
          membuf_append(&out, mapped, strlen(mapped));
        }
      }
      p = close;
    }
  }

  membuf_append(&out, p, strlen(p));
  membuf_append(&out, "", 1);

  return out;
}

/* ------------------------------------------------------------------- salida */

/* Vuelca un template (head/tail/404) en el archivo de salida. */
static void cat_into(FILE *dst, const char *template_name)
{
  char buf[8192];
  size_t n;
  FILE *in = fopen(template_name, "r");

  if (!in) {
    fprintf(stderr, "warning: no se pudo abrir %s\n", template_name);
    return;
  }
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, dst);
  fclose(in);
}

/* Escribe head + body + tail en out_path, creando los directorios que falten. */
static int write_page(const char *out_path, const char *body)
{
  char dir[PATH_MAX_LEN];
  char *slash;
  FILE *f;

  snprintf(dir, sizeof(dir), "%s", out_path);
  if ((slash = strrchr(dir, '/')) != NULL) {
    *slash = '\0';
    if (mkdir_p(dir) != 0) {
      fprintf(stderr, "error: mkdir %s: %s\n", dir, strerror(errno));
      return -1;
    }
  }

  if ((f = fopen(out_path, "w")) == NULL) {
    fprintf(stderr, "error: open %s: %s\n", out_path, strerror(errno));
    return -1;
  }

  cat_into(f, g_head);
  fputs(body, f);
  cat_into(f, g_tail);
  fclose(f);

  return 0;
}

static int copy_file(const char *src, const char *dst)
{
  char buf[65536];
  size_t n;
  FILE *in, *out;
  char dir[PATH_MAX_LEN];
  char *slash;

  snprintf(dir, sizeof(dir), "%s", dst);
  if ((slash = strrchr(dir, '/')) != NULL) {
    *slash = '\0';
    if (mkdir_p(dir) != 0) return -1;
  }

  if ((in = fopen(src, "rb")) == NULL) return -1;
  if ((out = fopen(dst, "wb")) == NULL) { fclose(in); return -1; }

  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
  }

  fclose(in);
  fclose(out);
  return 0;
}

/* ------------------------------------------------------------------ listado */

/* +2 en href: el nombre completo más el "/" final y el NUL. */
struct entry {
  char name[NAME_MAX_LEN];       /* texto visible */
  char href[NAME_MAX_LEN + 2];   /* relativo al directorio listado */
  int  is_dir;
};

struct entries {
  struct entry *v;
  size_t n, cap;
};

static void entries_push(struct entries *e, const char *name,
                         const char *href, int is_dir)
{
  if (e->n == e->cap) {
    size_t cap = e->cap ? e->cap * 2 : 16;
    struct entry *v = realloc(e->v, cap * sizeof(*v));
    if (!v) return;
    e->v = v;
    e->cap = cap;
  }
  snprintf(e->v[e->n].name, NAME_MAX_LEN, "%s", name);
  snprintf(e->v[e->n].href, sizeof(e->v[e->n].href), "%s", href);
  e->v[e->n].is_dir = is_dir;
  e->n++;
}

/* Genera el index.html de un directorio que no trae index.md publicado.
 *
 * Mismo markup que serve_dirlist() en main.c, para que los dos modos se vean
 * igual con el mismo CSS de head.html. */
static void write_listing(const char *out_dir, const char *urldir,
                          struct entries *e)
{
  struct membuffer body = {0};
  char out_path[PATH_MAX_LEN];
  size_t i;

  membuf_init(&body, 4096);

  put(&body, "<h1>");
  html_esc(&body, urldir);
  put(&body, "</h1>\n<ul class=\"dirlist\">\n");

  if (strcmp(urldir, "/") != 0)
    put(&body, "  <li class=\"up\"><a href=\"..\">..</a></li>\n");

  for (i = 0; i < e->n; i++) {
    put(&body, "  <li><a href=\"");
    url_enc(&body, e->v[i].href);
    put(&body, "\">");
    html_esc(&body, e->v[i].name);
    put(&body, "</a></li>\n");
  }

  put(&body, "</ul>\n");
  membuf_append(&body, "", 1);  /* NUL para tratar body.data como string */

  snprintf(out_path, sizeof(out_path), "%s/index.html", out_dir);
  write_page(out_path, body.data);

  membuf_fini(&body);
}

/* -------------------------------------------------------------------- walk */

static int name_cmp(const struct dirent **a, const struct dirent **b)
{
  return strcmp((*a)->d_name, (*b)->d_name);
}

/* Construye recursivamente src_dir → out_dir.
 *
 * urldir es la ruta URL del directorio *fuente*, con "/" a ambos extremos
 * ("/", "/notes/").
 *
 * Retorna la cantidad de *páginas* publicadas acá abajo. Los adjuntos no
 * cuentan: se copian, pero no se enumeran ni hacen que un directorio aparezca
 * en el listado del padre. Si contaran, un directorio de imágenes de una nota
 * privada quedaría navegable, que es justo lo que el opt-in evita. */
static int build_dir(const char *src_dir, const char *out_dir,
                     const char *urldir)
{
  struct dirent **names;
  struct entries listing = {0};
  int n, i, pages = 0, assets = 0, has_index = 0;

  if ((n = scandir(src_dir, &names, NULL, name_cmp)) < 0) {
    fprintf(stderr, "error: scandir %s: %s\n", src_dir, strerror(errno));
    return 0;
  }

  for (i = 0; i < n; i++) {
    const char *name = names[i]->d_name;
    char src[PATH_MAX_LEN], out[PATH_MAX_LEN];
    struct stat st;

    if (name[0] == '.') { free(names[i]); continue; }

    snprintf(src, sizeof(src), "%s/%s", src_dir, name);

    /* lstat() no sigue symlinks: mismo criterio que el server, un symlink
     * dentro del root no debe poder exportar algo de afuera. */
    if (lstat(src, &st) != 0 || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
      free(names[i]);
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      char sub_url[PATH_MAX_LEN];
      snprintf(out, sizeof(out), "%s/%s", out_dir, name);
      snprintf(sub_url, sizeof(sub_url), "%s%s/", urldir, name);

      if (build_dir(src, out, sub_url) > 0) {
        char href[NAME_MAX_LEN + 2];
        snprintf(href, sizeof(href), "%s/", name);
        entries_push(&listing, href, href, 1);
        pages++;
      } else {
        /* Sin páginas adentro. rmdir() sólo borra si quedó vacío, así que un
         * subárbol de puros adjuntos sobrevive sin aparecer en el listado. */
        rmdir(out);
      }
      free(names[i]);
      continue;
    }

    {
      size_t len = strlen(name);
      int is_md = len > 3 && strcmp(name + len - 3, ".md") == 0;

      if (!is_md) {
        /* Adjuntos: se copian siempre. Sin esto una nota publicada que
         * referencia una imagen queda con el link roto. No se enumeran: son
         * alcanzables por link, no por navegación. */
        snprintf(out, sizeof(out), "%s/%s", out_dir, name);
        if (copy_file(src, out) == 0) {
          assets++;
          g_assets++;
        }
        free(names[i]);
        continue;
      }

      {
        struct frontmatter fm;
        FILE *f = fopen(src, "r");
        char *html;
        char stem[NAME_MAX_LEN];
        int is_index;

        if (!f) { free(names[i]); continue; }

        fm_scan(f, &fm);
        if (!fm.publish) {
          fclose(f);
          g_skipped++;
          free(names[i]);
          continue;
        }

        html = md_to_html(f);   /* lee desde el cursor, ya pasado el frontmatter */
        fclose(f);

        if (!html) {
          fprintf(stderr, "error: no se pudo parsear %s\n", src);
          free(names[i]);
          continue;
        }

        snprintf(stem, sizeof(stem), "%.*s", (int)(len - 3), name);
        is_index = strcmp(stem, "index") == 0;

        if (is_index)
          snprintf(out, sizeof(out), "%s/index.html", out_dir);
        else
          snprintf(out, sizeof(out), "%s/%s/index.html", out_dir, stem);

        {
          struct membuffer fixed = rewrite_links(html, urldir);
          struct membuffer page = {0};

          membuf_init(&page, fixed.size + 128);
          put_date(&page, &fm);
          put(&page, fixed.data);
          membuf_append(&page, "", 1);
          write_page(out, page.data);
          membuf_fini(&page);
          membuf_fini(&fixed);
        }
        free(html);

        g_pages++;
        pages++;

        if (is_index) {
          has_index = 1;
        } else {
          char href[NAME_MAX_LEN + 2];
          snprintf(href, sizeof(href), "%s/", stem);
          entries_push(&listing, fm.title[0] ? fm.title : stem, href, 0);
        }
      }
    }
    free(names[i]);
  }
  free(names);

  /* Un index.md publicado gana: es la portada escrita a mano del directorio.
   * Un directorio de puros adjuntos no recibe índice: Apache lo cubre con
   * "Options -Indexes" y los archivos siguen siendo alcanzables por link. */
  if (!has_index && pages > 0)
    write_listing(out_dir, urldir, &listing);

  (void) assets;
  free(listing.v);
  return pages;
}

/* ------------------------------------------------------------------- extras */

/* .htaccess para Apache: 404 real (no 200 con cuerpo de error) y sin listados
 * automáticos, que filtrarían los adjuntos de notas no publicadas. */
static void write_htaccess(const char *out_dir)
{
  char path[PATH_MAX_LEN];
  FILE *f;

  snprintf(path, sizeof(path), "%s/.htaccess", out_dir);
  if ((f = fopen(path, "w")) == NULL) return;

  fputs("Options -Indexes\n"
        "ErrorDocument 404 /404.html\n"
        "DirectoryIndex index.html\n"
        "\n"
        "AddDefaultCharset utf-8\n"
        "<IfModule mod_headers.c>\n"
        "  Header set X-Content-Type-Options \"nosniff\"\n"
        "</IfModule>\n", f);

  fclose(f);
}

static void write_404(const char *out_dir)
{
  char path[PATH_MAX_LEN];
  FILE *f;

  snprintf(path, sizeof(path), "%s/404.html", out_dir);
  if ((f = fopen(path, "w")) == NULL) return;

  cat_into(f, g_head);
  cat_into(f, g_404);
  cat_into(f, g_tail);
  fclose(f);
}

int main(int argc, char **argv)
{
  const char *src = argc > 1 ? argv[1] : "./md";
  const char *out = argc > 2 ? argv[2] : "./dist";
  struct stat st;

  if (argc > 1 && (strcmp(argv[1], "-h") == 0 ||
                   strcmp(argv[1], "--help") == 0)) {
    printf("uso: mdbuild [src-dir] [out-dir]\n"
           "  src-dir  árbol de notas markdown (default ./md)\n"
           "  out-dir  destino del sitio estático (default ./dist)\n"
           "\n"
           "Sólo se emiten los .md con 'publish: true' en el frontmatter.\n"
           "head.html, tail.html y 404.html se leen del directorio actual.\n");
    return 0;
  }

  if (stat(src, &st) != 0 || !S_ISDIR(st.st_mode)) {
    fprintf(stderr, "error: %s no es un directorio\n", src);
    return 1;
  }

  if (mkdir_p(out) != 0) {
    fprintf(stderr, "error: no se pudo crear %s: %s\n", out, strerror(errno));
    return 1;
  }

  build_dir(src, out, "/");

  /* Cero páginas casi siempre es un error de operación (frontmatter olvidado,
   * src-dir equivocado), no una intención. Se falla fuerte para que el deploy
   * no publique un sitio vacío encima de uno que funcionaba. */
  if (g_pages == 0) {
    fprintf(stderr, "error: ninguna página tiene 'publish: true' en %s\n", src);
    return 1;
  }

  write_404(out);
  write_htaccess(out);

  fprintf(stderr, "mdbuild: %d páginas, %d adjuntos, %d sin publicar → %s\n",
          g_pages, g_assets, g_skipped, out);

  return 0;
}
