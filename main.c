#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "frontmatter.h"
#include "md.h"
#include "membuf.h"
#include "mongoose.h"

/* mongoose log callback that writes to stderr instead of stdout.
 * stdout output is silently lost during mg_mgr_poll on Linux (likely
 * epoll fd collision with fd 1). stderr reaches journald reliably. */
static void log_to_stderr(char c, void *param) {
  (void) param;
  putc(c, stderr);
}

/* Private/reserved IPv4 ranges we refuse to connect to. */
static int is_private_ipv4(uint32_t ip_host) {
  uint32_t ip = ntohl(ip_host);
  return (ip >> 24) == 10                     /* 10.0.0.0/8       */
      || (ip >> 24) == 127                    /* 127.0.0.0/8      */
      || (ip >> 16) == 0xa9fe                 /* 169.254.0.0/16   */
      || (ip >> 20) == 0xac1                  /* 172.16.0.0/12    */
      || (ip >> 16) == 0xc0a8                 /* 192.168.0.0/16   */
      || (ip >> 22) == 0x1901                 /* 100.64.0.0/10    */
      || (ip >> 24) == 0                      /* 0.0.0.0/8        */
      || (ip >> 28) == 0xe                    /* 224.0.0.0/4      */
      || (ip >> 28) == 0xf;                   /* 240.0.0.0/4      */
}

/* Private/reserved IPv6 ranges. */
static int is_private_ipv6(const uint8_t ip[16]) {
  /* ::1/128 — loopback */
  static const uint8_t zero[15] = {0};
  if (memcmp(ip, zero, 15) == 0 && ip[15] == 1) return 1;
  /* ::/128 — unspecified */
  if (memcmp(ip, zero, 15) == 0 && ip[15] == 0) return 1;
  /* fc00::/7 — unique local */
  if ((ip[0] & 0xfe) == 0xfc) return 1;
  /* fe80::/10 — link-local */
  if (ip[0] == 0xfe && (ip[1] & 0xc0) == 0x80) return 1;
  /* ::ffff:0:0/96 — IPv4-mapped. Check the embedded IPv4 address. */
  if (memcmp(ip, zero, 10) == 0 && ip[10] == 0xff && ip[11] == 0xff) {
    uint32_t v4;
    memcpy(&v4, ip + 12, 4);
    return is_private_ipv4(v4);
  }
  return 0;
}

/* Returns 1 if the host portion of a URL looks like a raw IP address. */
static int is_ip_host(const char *host, size_t len) {
  if (len == 0) return 0;
  /* IPv6 in brackets: [::1] */
  if (host[0] == '[') return 1;
  /* IPv4 starts with digit */
  if (host[0] >= '0' && host[0] <= '9') {
    for (size_t i = 1; i < len; i++)
      if (host[i] != '.' && (host[i] < '0' || host[i] > '9'))
        return 0;
    return 1;
  }
  return 0;
}

/* Defaults para correr desde el repo. En el servidor se pasan por argv:
 *
 *   mdserve http://100.64.0.12:8080 /home/user/notes
 *
 * Conviene pasar una IP concreta y no 0.0.0.0: en una VM con IP pública,
 * el wildcard publica el servidor en todas las interfaces, y mdserve no
 * tiene ni auth ni TLS. */
#define ROOT_DIR   "./md"
#define LISTEN_URL "http://0.0.0.0:8080"
#define MAX_REMOTE_BODY (16 * 1024 * 1024)  /* 16 MB limit for remote fetches */

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

/* Renderiza la fecha del frontmatter como texto y como valor machine-readable.
 * Ambos contextos se escapan: date es contenido controlado por el autor de la
 * nota, no HTML confiable. */
static void write_date(struct mg_connection *c, const struct frontmatter *fm)
{
  if (!fm->date[0]) return;

  mg_http_printf_chunk(c,
      "<p class=\"date\"><time datetime=\"%M\">%M</time></p>\n",
      mg_print_html_esc, (int) strlen(fm->date), fm->date,
      mg_print_html_esc, (int) strlen(fm->date), fm->date);
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
               "X-Content-Type-Options: nosniff\r\n"
               "Transfer-Encoding: chunked\r\n\r\n");
  cat_file(c, "head.html");
  cat_file(c, "404.html");
  cat_file(c, "tail.html");
  mg_http_printf_chunk(c, "");
}

static void serve_403(struct mg_connection *c)
{
  mg_http_reply(c, 403, "Content-Type: text/html\r\n",
                "<h1>Error 403: Forbidden</h1>\n");
}

static void serve_400(struct mg_connection *c)
{
  mg_http_reply(c, 400, "Content-Type: text/html\r\n",
                "<h1>Error 400: Bad Request</h1>\n");
}

/* Renderiza a HTML un .md ya resuelto a ruta local, envuelto en head.html y
 * tail.html.
 *
 * Se responde en chunks para no tener que calcular el Content-Length de la
 * concatenación de los tres pedazos por adelantado. */
static void render_markdown(struct mg_connection *c, const char *path)
{
  struct stat st;
  struct frontmatter fm;
  FILE *file;
  char *html;

  /* lstat() no sigue symlinks: un symlink a /etc/passwd dentro del root
   * sería servido si usáramos stat(). lstat() lo detecta y rechaza. */
  if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    serve_404(c);
    return;
  }
  /* Double-check with lstat: S_ISLNK shouldn't happen since lstat on a
   * symlink returns the link itself (not a regular file), but belt-and-suspenders. */
  if (S_ISLNK(st.st_mode)) {
    serve_403(c);
    return;
  }

  if ((file = fopen(path, "r")) == NULL) {
    serve_404(c);
    return;
  }

  /* El server ignora el flag "publish" — navegar local muestra todo — pero
   * igual tiene que saltarse el bloque: md4c leería "---\ntitle: x\n---" como
   * thematic break más setext heading, o sea basura arriba de cada nota. */
  fm_scan(file, &fm);

  html = md_to_html(file);
  fclose(file);

  if (!html) {
    mg_http_reply(c, 500, "Content-Type: text/html\r\n",
                  "<h1>Error 500: no se pudo parsear el markdown</h1>\n");
    return;
  }

  mg_printf(c, "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "X-Content-Type-Options: nosniff\r\n"
               "Transfer-Encoding: chunked\r\n\r\n");
  cat_file(c, "head.html");
  write_date(c, &fm);
  mg_http_printf_chunk(c, "%s", html);
  cat_file(c, "tail.html");
  mg_http_printf_chunk(c, "");  /* chunk vacío = fin de la respuesta */

  free(html);
}

static void serve_markdown(struct mg_connection *c, struct mg_http_message *hm,
                           const char *root)
{
  char path[MG_PATH_MAX];

  if (!uri_to_local_path(hm->uri, root, path, sizeof(path))) {
    serve_403(c);
    return;
  }

  render_markdown(c, path);
}

/* Context for remote fetch: holds the original server connection so we can
 * reply when the fetch completes. */
struct remote_ctx {
  struct mg_connection *server_conn;  /* original server connection */
  char *url;                          /* remote URL being fetched */
};

/* Client event handler for the outbound HTTP fetch.
 * On MG_EV_CONNECT, send the GET request.
 * On MG_EV_HTTP_MSG, process response and reply to server_conn. */
static void remote_client_handler(struct mg_connection *nc, int ev, void *ev_data)
{
  struct remote_ctx *ctx = (struct remote_ctx *) nc->fn_data;
  struct mg_http_message *hm = (struct mg_http_message *) ev_data;
  char *html = NULL;
  struct frontmatter fm;

  if (ev == MG_EV_CONNECT) {
    /* Connection established — reject private/reserved IPs. */
    if ((!nc->rem.is_ip6 && is_private_ipv4(nc->rem.addr.ip4))
        || (nc->rem.is_ip6 && is_private_ipv6(nc->rem.addr.ip))) {
      MG_ERROR(("rejected private IP for %s", ctx->url));
      mg_http_reply(ctx->server_conn, 403,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 403: Private IPs are not allowed</h1>\n");
      free(ctx->url);
      free(ctx);
      nc->is_draining = 1;
      return;
    }
    if (nc->is_client && nc->is_udp == 0) {
      if (strncmp(ctx->url, "https://", 8) == 0) {
        /* For HTTPS, init TLS and wait for MG_EV_TLS_HS before sending GET */
        /* Extract hostname for SNI */
        const char *h = ctx->url + 8;
        const char *slash = strchr(h, '/');
        char sni_host[256];
        if (slash)
          mg_snprintf(sni_host, sizeof(sni_host), "%.*s", (int)(slash - h), h);
        else
          mg_snprintf(sni_host, sizeof(sni_host), "%s", h);
        struct mg_tls_opts opts = { .skip_verification = true,
                                    .name = mg_str(sni_host) };
        mg_tls_init(nc, &opts);
        MG_INFO(("TLS init done for %s", ctx->url));
      } else {
        /* For plain HTTP, send GET immediately */
        const char *host_start = strstr(ctx->url, "://");
        if (host_start) host_start += 3;
        else host_start = ctx->url;
        const char *host_end = strchr(host_start, '/');
        char host[256];
        if (host_end)
          mg_snprintf(host, sizeof(host), "%.*s", (int)(host_end - host_start), host_start);
        else
          mg_snprintf(host, sizeof(host), "%s", host_start);
        mg_printf(nc, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  host_end ? host_end : "/", host);
        MG_INFO(("sent HTTP GET to %s", host));
      }
    }
  } else if (ev == MG_EV_TLS_HS) {
    /* TLS handshake succeeded; send GET request */
    const char *host_start = strstr(ctx->url, "://");
    if (host_start) host_start += 3;
    else host_start = ctx->url;
    const char *host_end = strchr(host_start, '/');
    char host[256];
    if (host_end)
      mg_snprintf(host, sizeof(host), "%.*s", (int)(host_end - host_start), host_start);
    else
      mg_snprintf(host, sizeof(host), "%s", host_start);
    mg_printf(nc, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
              host_end ? host_end : "/", host);
    MG_INFO(("TLS handshake done, sent HTTPS GET to %s", host));
  } else if (ev == MG_EV_ERROR) {
    MG_ERROR(("remote connection error: %s", ctx->url));
    mg_http_reply(ctx->server_conn, 502,
                  "Content-Type: text/html\r\n"
                  "X-Content-Type-Options: nosniff\r\n",
                  "<h1>Error 502: Remote connection failed</h1>\n");
    free(ctx->url);
    free(ctx);
    nc->is_draining = 1;
  } else if (ev == MG_EV_HTTP_MSG) {
    /* Full HTTP response received */
    if (hm == NULL || hm->body.len == 0) {
      MG_ERROR(("remote fetch failed or empty: %s", ctx->url));
      mg_http_reply(ctx->server_conn, 502,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 502: Failed to fetch remote URL</h1>\n");
      goto cleanup;
    }

    if (hm->message.len > 0 && hm->message.buf[0] != 'H') {
      /* Not an HTTP response (e.g. TLS error) */
      MG_ERROR(("remote fetch error: %.*s", (int) hm->message.len, hm->message.buf));
      mg_http_reply(ctx->server_conn, 502,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 502: Remote fetch failed</h1>\n");
      goto cleanup;
    }

    if (hm->message.len > 0 && strncmp(hm->message.buf, "HTTP/1.1 200", 12) != 0 &&
        strncmp(hm->message.buf, "HTTP/1.0 200", 12) != 0) {
      MG_ERROR(("remote HTTP error: %.*s", (int) hm->message.len, hm->message.buf));
      mg_http_reply(ctx->server_conn, 502,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 502: Remote server error</h1>\n");
      goto cleanup;
    }

    /* Reject oversized responses before parsing */
    if (hm->body.len > MAX_REMOTE_BODY) {
      MG_ERROR(("remote body too large: %lu bytes", (unsigned long) hm->body.len));
      mg_http_reply(ctx->server_conn, 413,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 413: Remote file too large</h1>\n");
      goto cleanup;
    }

    /* Parse fetched markdown to HTML */
    struct membuffer buf = { .data = (char *) hm->body.buf, .asize = hm->body.len, .size = hm->body.len };
    FILE *mem = fmemopen(buf.data, buf.size, "r");
    if (!mem) {
      mg_http_reply(ctx->server_conn, 500,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 500: Cannot read response body</h1>\n");
      goto cleanup;
    }

    fm_scan(mem, &fm);
    html = md_to_html(mem);
    fclose(mem);

    if (!html) {
      mg_http_reply(ctx->server_conn, 500,
                    "Content-Type: text/html\r\n"
                    "X-Content-Type-Options: nosniff\r\n",
                    "<h1>Error 500: Failed to parse markdown</h1>\n");
      goto cleanup;
    }

    /* Send rendered HTML wrapped in head/tail templates */
    mg_printf(ctx->server_conn, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "Transfer-Encoding: chunked\r\n\r\n");
    cat_file(ctx->server_conn, "head.html");
    mg_http_printf_chunk(ctx->server_conn, "<p>Fuente: <a href=\"%s\">%s</a></p>\n",
                         ctx->url, ctx->url);
    write_date(ctx->server_conn, &fm);
    mg_http_printf_chunk(ctx->server_conn, "%s", html);
    cat_file(ctx->server_conn, "tail.html");
    mg_http_printf_chunk(ctx->server_conn, "");

cleanup:
    free(html);
    free(ctx->url);
    free(ctx);
    /* Close the client connection (server connection stays open until chunked response ends) */
    nc->is_draining = 1;
  }
}

/* Serve a remote markdown file via /remote/<url-encoded-remote-url> */
static void serve_remote(struct mg_connection *c, struct mg_http_message *hm,
                         const char *root)
{
  (void) root;
  /* URI format: /remote/<url-encoded-remote-url> */
  const char prefix[] = "/remote/";
  if (hm->uri.len <= sizeof(prefix) - 1) {
    serve_404(c);
    return;
  }

  /* Extract and URL-decode the remote URL */
  struct mg_str encoded = mg_str_n(hm->uri.buf + sizeof(prefix) - 1,
                                    hm->uri.len - (sizeof(prefix) - 1));
  char *url = malloc(encoded.len + 1);
  if (!url) {
    mg_http_reply(c, 500, "Content-Type: text/html\r\n",
                  "<h1>Error 500: Out of memory</h1>\n");
    return;
  }
  int n = mg_url_decode(encoded.buf, encoded.len, url, encoded.len + 1, 0);
  if (n < 0) {
    free(url);
    serve_400(c);
    return;
  }
  url[n] = '\0';

  /* Basic scheme validation */
  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
    free(url);
    serve_400(c);
    return;
  }

  /* Extract host and path for validation */
  const char *host_start = strstr(url, "://");
  host_start = host_start ? host_start + 3 : url;
  const char *path_start = strchr(host_start, '/');
  size_t host_len = path_start ? (size_t)(path_start - host_start) : strlen(host_start);

  /* Reject raw IP addresses as host */
  if (is_ip_host(host_start, host_len)) {
    free(url);
    serve_403(c);
    return;
  }

  /* Only allow .md files */
  if (!path_start || path_start[1] == '\0') {
    free(url);
    serve_400(c);
    return;
  }
  size_t path_len = strlen(path_start);
  if (path_len < 3 || strcmp(path_start + path_len - 3, ".md") != 0) {
    free(url);
    serve_400(c);
    return;
  }

  MG_INFO(("fetching remote markdown: %s", url));

  /* Allocate context for the fetch callback */
  struct remote_ctx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    free(url);
    mg_http_reply(c, 500, "Content-Type: text/html\r\n",
                  "<h1>Error 500: Out of memory</h1>\n");
    return;
  }
  ctx->server_conn = c;
  ctx->url = url;

  /* Start async fetch; callback will send the response to server_conn */
  struct mg_connection *nc = mg_http_connect(c->mgr, url, remote_client_handler, ctx);
  if (!nc) {
    MG_ERROR(("mg_http_connect failed for %s", url));
    mg_http_reply(c, 500, "Content-Type: text/html\r\n",
                  "<h1>Error 500: Fetch initiation failed</h1>\n");
    free(ctx->url);
    free(ctx);
    return;
  }
  /* Connection stays open; response sent in remote_client_handler */
}

/* Listado de directorio usando los mismos templates que las notas.
 *
 * mg_http_serve_dir() ya trae uno, pero con su propio <style> embebido y un
 * footer de mongoose, así que no pega ni con head.html ni con el resto.
 *
 * Esta función NO resuelve rutas: recibe un path que route() ya pasó por
 * uri_to_local_path(), o sea que el chequeo de traversal ya corrió. Lo único
 * que hace es leer el directorio.
 *
 * Los href son relativos al uri, así que route() garantiza que el uri termine
 * en '/' antes de llegar acá. */
static void serve_dirlist(struct mg_connection *c, struct mg_str uri,
                          const char *path)
{
  struct dirent **names;
  int n = scandir(path, &names, NULL, alphasort);
  int i;

  if (n < 0) {
    serve_404(c);
    return;
  }

  mg_printf(c, "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=utf-8\r\n"
               "X-Content-Type-Options: nosniff\r\n"
               "Transfer-Encoding: chunked\r\n\r\n");
  cat_file(c, "head.html");
  mg_http_printf_chunk(c, "<h1>%M</h1>\n<ul class=\"dirlist\">\n",
                       mg_print_html_esc, (int) uri.len, uri.buf);

  if (uri.len > 1)
    mg_http_printf_chunk(c, "  <li class=\"up\"><a href=\"..\">..</a></li>\n");

  for (i = 0; i < n; i++) {
    const char *name = names[i]->d_name;
    char enc[MG_PATH_MAX], full[MG_PATH_MAX];
    const char *slash = "";
    struct stat st;

    /* Se saltan "." y ".." de paso. */
    if (name[0] == '.') {
      free(names[i]);
      continue;
    }

    mg_snprintf(full, sizeof(full), "%s/%s", path, name);
    /* lstat() does not follow symlinks — prevents symlink escape from root. */
    if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) {
      free(names[i]);
      continue;
    }
    if (S_ISDIR(st.st_mode)) slash = "/";

    /* El nombre va URL-encodeado en el href y HTML-escapeado en el texto: un
     * archivo puede llamarse '<img onerror=...>.md' y sería XSS reflejado. */
    mg_url_encode(name, strlen(name), enc, sizeof(enc));
    mg_http_printf_chunk(c, "  <li><a href=\"%s%s\">%M%s</a></li>\n",
                         enc, slash,
                         mg_print_html_esc, (int) strlen(name), name, slash);
    free(names[i]);
  }
  free(names);

  mg_http_printf_chunk(c, "</ul>\n");
  cat_file(c, "tail.html");
  mg_http_printf_chunk(c, "");
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
  if (mg_match(hm->uri, mg_str("/remote/#"), NULL)) {
    serve_remote(c, hm, root);
  } else if (mg_match(hm->uri, mg_str("#.md"), NULL)) {
    serve_markdown(c, hm, root);
  } else if (!uri_to_local_path(hm->uri, root, path, sizeof(path))) {
    serve_403(c);
  } else if (lstat(path, &st) != 0) {
    /* Cortocircuito para devolver el 404 con el status correcto.
     *
     * Este chequeo sólo puede agregar 404s, nunca filtrar de más: si el
     * archivo existe, igual delegamos y mongoose vuelve a resolver el path
     * con sus propios chequeos de traversal. */
    serve_404(c);
  } else if (S_ISDIR(st.st_mode)) {
    char index[MG_PATH_MAX];
    struct stat ist;

    if (hm->uri.buf[hm->uri.len - 1] != '/') {
      /* Sin la barra final el browser resuelve los href relativos un nivel
       * más arriba: /sub + "nota.md" daría /nota.md. */
      mg_printf(c, "HTTP/1.1 301 Moved Permanently\r\n"
                   "Location: %.*s/\r\n"
                   "Content-Length: 0\r\n\r\n",
                (int) hm->uri.len, hm->uri.buf);
      return;
    }

    mg_snprintf(index, sizeof(index), "%s/index.md", path);

    if (lstat(index, &ist) == 0 && S_ISREG(ist.st_mode)) {
      render_markdown(c, index);
    } else {
      serve_dirlist(c, hm->uri, path);
    }
  } else {
    /* Archivos estáticos sueltos: mg_http_serve_dir() vuelve a resolver el
     * path con sus propios chequeos, adivina el MIME y soporta Range. */
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
  mg_log_set(MG_LL_INFO);
  /* IPv6-only VPS: use DNS64 so IPv4-only hosts (GitHub) resolve. */
  mgr.use_dns6 = true;
  mgr.dns6.url = "udp://[2001:4860:4860::6464]:53";  /* Google DNS64 */
  /* mongoose logs go to stdout by default, but during mg_mgr_poll
   * stdout output is lost (epoll/socket machinery seems to interact
   * with fd 1). Redirect to stderr which journald captures reliably. */
  mg_log_set_fn(log_to_stderr, NULL);

  if (mg_http_listen(&mgr, url, route, (void *) root) == NULL) {
    MG_ERROR(("no se pudo escuchar en %s", url));
    return 1;
  }

  MG_INFO(("mdserve escuchando en %s, root=%s", url, root));
  for (;;) mg_mgr_poll(&mgr, 1000);

  mg_mgr_free(&mgr);
  return 0;
}