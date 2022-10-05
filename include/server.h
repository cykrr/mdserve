#include <stdio.h>

#define CONNMAX 50
#define BUFSZ 65535
#define BUFFER_SIZE 50
#define MAX_URI_SIZE 50
#define LINE_MAX 1024

typedef struct { char *name, *value; } header_t;


typedef struct Server {
    int listenfd;
    int clientfd;

    int clients[CONNMAX];
    int client_count;

    FILE* clientfp;

    char* method,    // "GET" or "POST"
    * uri,       // "/index.html" things before '?'
    * qs,        // "a=1&b=2"     things after  '?'
    * prot;      // "HTTP/1.1"
    
    const char *port;
    void (*route)(char *uri, FILE *out);
    
    header_t reqhdr[17];
} Server;


void serverInit(Server *s, const char *port, void (*route)(char *uri, FILE *out));
void serve(Server *server);
void startServer(Server *server);
void respond(Server *server, int n);
