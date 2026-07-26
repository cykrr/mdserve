#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "md.h"
#include "mongoose.h"

/* Directorio que se expone por HTTP. Sólo esto es visible desde afuera: el
 * resto del repo (.git/, fuentes, dotfiles) queda fuera del alcance.
 * Los templates (head.html, tail.html, 404.html) no pasan por acá, se abren
 * con fopen() relativo al cwd. */
#define ROOT_DIR   "./md"
#define LISTEN_URL "http://0.0.0.0:8080"

/* Vuelca un archivo del disco en la respuesta chunked ya iniciada.
 * Se usa para head.html / tail.html, que envuelven el markdown renderizado. */
static void cat_file(struct mg_connection *c, const char *filename)
{
  char line[2048];
  FILE *in = fopen(filename, "r");

  if (!in) return;

  while (fgets(line, sizeof(line), in)) mg_http_printf_chunk(c, "%s", line);
  fclose(in);
}

/* Traduce el URI de una request a una ruta local bajo ROOT_DIR.
 *
 * Mismo procedimiento que usa mongoose internamente: primero decodifica los
 * %XX, después valida con mg_path_is_sane() (rechaza "~" inicial, ".." inicial
 * y cualquier segmento "/.."), y recién ahí concatena el root.
 *
 * El orden importa: validar antes de decodificar deja pasar "%2e%2e", que se
 * convierte en ".." justo a tiempo para el fopen().
 *
 * Retorna 0 si el URI no entra en el buffer o intenta escapar del root. */
static int uri_to_local_path(struct mg_str uri, char *dst, size_t dst_len)
{
  char decoded[MG_PATH_MAX];
  int n = mg_url_decode(uri.buf, uri.len, decoded, sizeof(decoded), 0);

  if (n < 0) return 0;  /* no entra en el buffer, o %XX mal formado */
  if (!mg_path_is_sane(mg_str_n(decoded, (size_t) n))) return 0;

  mg_snprintf(dst, dst_len, "%s%s", ROOT_DIR, decoded);
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
static void serve_markdown(struct mg_connection *c, struct mg_http_message *hm)
{
  char path[MG_PATH_MAX];
  FILE *file;
  char *html;

  if (!uri_to_local_path(hm->uri, path, sizeof(path))) {
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
 * con la request entera ya parseada (línea, headers y body). */
static void route(struct mg_connection *c, int ev, void *ev_data)
{
  struct mg_http_message *hm;
  struct mg_http_serve_opts opts = { .root_dir = ROOT_DIR };
  char path[MG_PATH_MAX];
  struct stat st;

  if (ev != MG_EV_HTTP_MSG) return;
  hm = (struct mg_http_message *) ev_data;

  MG_INFO(("%.*s %.*s", (int) hm->method.len, hm->method.buf,
                        (int) hm->uri.len, hm->uri.buf));

  /* En los patrones de mg_match(), '#' matchea cualquier secuencia
   * incluyendo '/'; '*' se detiene en la barra. */
  if (mg_match(hm->uri, mg_str("#.md"), NULL)) {
    serve_markdown(c, hm);
  } else if (!uri_to_local_path(hm->uri, path, sizeof(path))) {
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

int main(void)
{
  struct mg_mgr mgr;

  mg_mgr_init(&mgr);

  if (mg_http_listen(&mgr, LISTEN_URL, route, NULL) == NULL) {
    MG_ERROR(("no se pudo escuchar en %s", LISTEN_URL));
    return 1;
  }

  MG_INFO(("mdserve escuchando en %s, root=%s", LISTEN_URL, ROOT_DIR));
  for (;;) mg_mgr_poll(&mgr, 1000);

  mg_mgr_free(&mgr);
  return 0;
}
