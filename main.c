#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>


#include <dirent.h>


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "md.h"
#include "server.h"


void cat_file(FILE* out, char *filename) {
  if (!out) {
    printf("Excepción: cat_file(): El archivo no existe o puntero NULL");
    return;
  }

  char line[2048] = {0};

  FILE *in = fopen(filename, "r");
  if (!in) {
      return;
  }

  while (fgets(line, sizeof(line), in)) {
    fprintf(out, "%s", line);
  }
  fclose(in);
}

static void route(const char *uri, FILE *out) {
  printf("Uri: %s\n", uri);

  if (strstr(uri, "..")) {
      fprintf(out, "HTTP/1.1 403 FORBIDDEN \r\n\r\n");
      fprintf(out, "<h1>Error 403: Forbidden</h1>\r\n");
      return;
  }

  char filename[256] = ".";
  strncat(filename, uri, 254);

  DIR *dir = opendir(filename);
  struct dirent *dent;

  if (dir) {
    fprintf(out, "HTTP/1.1 200 OK \r\n\r\n");
    while ((dent = readdir(dir)) != NULL)  {
      if (strcmp(dent->d_name, ".") == 0 ||
          strcmp(dent->d_name, "..") == 0 ||
          (*dent->d_name == '.')) {
      } else {
        if (strcmp(uri, "/") == 0) {
            fprintf(out, "<a href = \"%s%s\">%s</a><br>\r\n", uri, dent->d_name, dent->d_name);
        } else {
            fprintf(out, "<a href = \"%s/%s\">%s</a><br>\r\n", uri, dent->d_name, dent->d_name);
        }
      }
    }

    closedir(dir);
    return;
  }

  FILE *file = fopen(filename, "r");
  if (file)
    fprintf(out, "HTTP/1.1 200 OK \r\n\r\n");
  else {
    fprintf(out, "HTTP/1.1 404 NOT FOUND \r\n\r\n");
    cat_file(out, "404.html");
    return;
  }

  size_t uri_len = strlen(uri);
  int is_md = (uri_len > 3 && strcmp(uri + uri_len - 3, ".md") == 0);

  if (is_md){
    cat_file(out, "head.html");

    fprintf(
        out,
        "%s",
        md_to_html(file)
        );

    cat_file(out, "tail.html");
    fclose(file);
  } else {
      char line[2048] = {0};
      while (fgets(line, sizeof(line), file)) {
        fprintf(out, "%s", line);
      }
      fclose(file);
  }
}

int main(int argc, char *argv[])
{
    const char *port = "8080";
    const char *dir = ".";
    int opt;

    while ((opt = getopt(argc, argv, "p:d:")) != -1) {
        switch (opt) {
            case 'p':
                port = optarg;
                break;
            case 'd':
                dir = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-p port] [-d directory]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (chdir(dir) != 0) {
        perror("chdir failed");
        exit(EXIT_FAILURE);
    }

    Server s;
    for (int i = 0; i < CONNMAX; i++) {
        s.clients[i] = -1;
    }
    serverInit(&s, port, &route);

    printf("Init successful\n");
    printf("=========================\n");
    printf("Port: %s\n", port);
    printf("Directory: %s\n", dir);
    printf("=========================\n");
    fflush(stdout);

    serve(&s);
    return 0;
}



FILE * open_wrapper(char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
      printf("\"%s\" not found\n", filename);
      return NULL;
    }

    return f;
}


char *rawFile(char *filename) {
  FILE* file = fopen(filename, "w");
  if (!file) {
    return "File not found";
  }

  return "";
}


// static char *buf;
//
//
//
//
// #define ROUTE_START()       if (0) {
// #define ROUTE(METHOD,URI)   } else if (strcmp(URI,uri)==0&&strcmp(METHOD,method)==0) {
// #define ROUTE_GET(URI)      ROUTE("GET", URI) 
// #define ROUTE_POST(URI)     ROUTE("POST", URI) 
// #define ROUTE_HEAD(URI)     ROUTE("HEAD", URI)
//
// #define ROUTE_END()         } else printf(\
//                                 "HTTP/1.1 500 Not Handled\r\n\r\n" \
//                                 "The server has no handler to the request: %s .\r\n", uri \
//                             );
