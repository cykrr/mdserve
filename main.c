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


static void route(const char *uri, FILE *out) {
    if(strcmp(uri, "/") == 0) {
        fprintf(out, "HTTP/1.1 200 OK \r\n\r\n");
        fprintf(out, "<h1>asd</h1> \r\n\r\n");
        return;
    }
}


int main()
{
    Server s;
        serverInit(&s, "8080", &route);
        printf("Init successful\n");
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
