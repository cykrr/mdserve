#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "md.h"
#include "mongoose.h"

/* Defaults para correr desde el repo. En el servidor se pasan por argv:
 *
 *   mdserve http://100.64.0.12:8080 /home/user/notes
 *
 * Conviene pasar una IP concreta y no 0.0.0.0: en una VM con IP pública,
 * el wildcard publica el servidor en todas las interfaces, y mdserve no
 * tiene ni auth ni TLS. */
#define ROOT_DIR   "./md"
#define LISTEN_URL "http://0.0.0.0:8080"

/* Vuelca un archivo del disco en la respuesta chunked ya iniciada.
 * Se usa para head.html / tail.html / 404.html, que son del instalado y no de
 * las notas: se abren relativo al cwd, no al root que se sirve. */
static void cat_file(struct mg_connection *c, const char *filename)
{
  char line[2048];
  FILE *in = fopen(filename, "r");

  if (!in) return;

  while (fgets(line, sizeof(line), in)) mg_http_printf_chunk(c, "%s", line);
  fclose(in);
}

/* Traduce el URI de una request a una ruta local bajo root.
 *
 * Mismo procedimiento que usa mongoose internamente: primero decodifica los
 * %XX, después valida con mg_path_is_sane() (rechaza "~" inicial, ".." inicial
 * y cualquier segmento "/.."), y recién ahí concatena el root.
 *
 * El orden importa: validar antes de decodificar deja pasar "%2e%2e", que se
 * convierte en ".." justo a tiempo para el fopen().
 *
 * Retorna 0 si el URI no entra en el buffer o intenta escapar del root. */
static int uri_to_local_path(struct mg_str uri, const char *root,
                             char *dst, size_t dst_len)
{
  char decoded[MG_PATH_MAX];
  int n = mg_url_decode(uri.buf, uri.len, decoded, sizeof(decoded), 0);

  if (n < 0) return 0;  /* no entra en el buffer, o %XX mal formado */
  if (!mg_path_is_sane(mg_str_n(decoded, (size_t) n))) return 0;

  mg_snprintf(dst, dst_len, "%s%s", root, decoded);
  return 1;
}

/* Responde con la página 404 del proyecto.
 *
 * No usamos opts.page404 de mongoose: esa opción sirve el archivo pero deja el
 * status en 200 (mongoose.c:4127), o sea le miente a crawlers y caches. */
static void serve_404(struct mg_connection *c)
{
  mg_printf(c, "HTTP/1.1 404 Not Found\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Transfer-Encoding: chunked\r\n\r\n");
  cat_file(c, "404.html");
  mg_http_printf_chunk(c, "");
}

static void serve_403(struct mg_connection *c)
{
  mg_http_reply(c, 403, "Content-Type: text/html\r\n",
                "<h1>Error 403: Forbidden</h1>\n");
}

/* Renderiza un .md a HTML, envuelto en head.html y tail.html.
 *
 * Se responde en chunks para no tener que calcular el Content-Length de la
 * concatenación de los tres pedazos por adelantado. */
static void serve_markdown(struct mg_connection *c, struct mg_http_message *hm,
                           const char *root)
{
  char path[MG_PATH_MAX];
  FILE *file;
  char *html;

  if (!uri_to_local_path(hm->uri, root, path, sizeof(path))) {
    serve_403(c);
    return;
  }

  if ((file = fopen(path, "r")) == NULL) {
    serve_404(c);
    return;
  }

  html = md_to_html(file);
  fclose(file);

  if (!html) {
    mg_http_reply(c, 500, "Content-Type: text/html\r\n",
                  "<h1>Error 500: no se pudo parsear el markdown</h1>\n");
    return;
  }

  mg_printf(c, "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "Transfer-Encoding: chunked\r\n\r\n");
  cat_file(c, "head.html");
  mg_http_printf_chunk(c, "%s", html);
  cat_file(c, "tail.html");
  mg_http_printf_chunk(c, "");  /* chunk vacío = fin de la respuesta */

  free(html);
}

/* Handler de eventos de mongoose. Sólo nos interesa MG_EV_HTTP_MSG, que llega
 * con la request entera ya parseada (línea, headers y body).
 *
 * El root a servir viaja en c->fn_data, que es el puntero que se le pasó a
 * mg_http_listen(). */
static void route(struct mg_connection *c, int ev, void *ev_data)
{
  struct mg_http_message *hm;
  const char *root = (const char *) c->fn_data;
  struct mg_http_serve_opts opts = { .root_dir = root };
  char path[MG_PATH_MAX];
  struct stat st;

  if (ev != MG_EV_HTTP_MSG) return;
  hm = (struct mg_http_message *) ev_data;

  MG_INFO(("%.*s %.*s", (int) hm->method.len, hm->method.buf,
                        (int) hm->uri.len, hm->uri.buf));

  /* En los patrones de mg_match(), '#' matchea cualquier secuencia
   * incluyendo '/'; '*' se detiene en la barra. */
  if (mg_match(hm->uri, mg_str("#.md"), NULL)) {
    serve_markdown(c, hm, root);
  } else if (!uri_to_local_path(hm->uri, root, path, sizeof(path))) {
    serve_403(c);
  } else if (stat(path, &st) != 0) {
    /* Cortocircuito para devolver el 404 con el status correcto.
     *
     * Este chequeo sólo puede agregar 404s, nunca filtrar de más: si el
     * archivo existe, igual delegamos y mongoose vuelve a resolver el path
     * con sus propios chequeos de traversal. */
    serve_404(c);
  } else {
    /* Archivos estáticos y listado de directorios. mg_http_serve_dir() ya
     * resuelve el path, chequea traversal, adivina el MIME y soporta Range. */
    mg_http_serve_dir(c, hm, &opts);
  }
}

int main(int argc, char *argv[])
{
  struct mg_mgr mgr;
  const char *url  = argc > 1 ? argv[1] : LISTEN_URL;
  const char *root = argc > 2 ? argv[2] : ROOT_DIR;
  struct stat st;

  if (argc > 3) {
    fprintf(stderr, "uso: %s [listen-url] [root-dir]\n", argv[0]);
    return 2;
  }

  /* Fallar acá y no en cada request: si el root no existe, todo responde 404
   * y parece un problema de rutas. */
  if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    MG_ERROR(("root-dir no es un directorio: %s", root));
    return 1;
  }

  mg_mgr_init(&mgr);

  if (mg_http_listen(&mgr, url, route, (void *) root) == NULL) {
    MG_ERROR(("no se pudo escuchar en %s", url));
    return 1;
  }

  MG_INFO(("mdserve escuchando en %s, root=%s", url, root));
  for (;;) mg_mgr_poll(&mgr, 1000);

  mg_mgr_free(&mgr);
  return 0;
}
