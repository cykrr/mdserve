#include <stdio.h>

#define CONNMAX 50
#define BUFSZ 65535
#define BUFFER_SIZE 50
#define MAX_URI_SIZE 50
#define LINE_MAX 1024

typedef struct { char *name, *value; } header_t;


typedef struct Server {
    int listenfd;
    int clients[CONNMAX];
    int client_count;

    const char *port;
    void (*route)(const char *uri, FILE *out);
} Server;

typedef struct thread_args {
    Server *server;
    int n;
} thread_args_t;


void serverInit(Server *s, const char *port, void (*route)(const char *uri, FILE *out));
void serve(Server *server);
void startServer(Server *server);
void *respond(void *args);
