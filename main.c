#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include <arpa/inet.h>

#define CONNMAX 50
#define BUFSZ 65535
#define BUFFER_SIZE 50
#define MAX_URI_SIZE 50

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "md.h"

int clientfd;
FILE *clientfp;
int clients[CONNMAX];
int client_count = 0;

static int listenfd;

char *rawFile() {

}

static char *buf;

char* method,    // "GET" or "POST"
* uri,       // "/index.html" things before '?'
* qs,        // "a=1&b=2"     things after  '?'
* prot;      // "HTTP/1.1"

typedef struct { char *name, *value; } header_t;
static header_t reqhdr[17] = { {"\0", "\0"} };

char    *payload;     // for POST
int      payload_size;

#define ROUTE_START()       if (0) {
#define ROUTE(METHOD,URI)   } else if (strcmp(URI,uri)==0&&strcmp(METHOD,method)==0) {
#define ROUTE_GET(URI)      ROUTE("GET", URI) 
#define ROUTE_POST(URI)     ROUTE("POST", URI) 
#define ROUTE_HEAD(URI)     ROUTE("HEAD", URI)

#define ROUTE_END()         } else printf(\
                                "HTTP/1.1 500 Not Handled\r\n\r\n" \
                                "The server has no handler to the request: %s .\r\n", uri \
                            );


/* Get request header */
char *request_header(const char *name)
{
    header_t *h = reqhdr;
    while (h->name)
    {
        if (strcmp(h->name, name) == 0)
            return h->value;
        h++;
    }
    return NULL;
}

void route() {
  fprintf(clientfp, "HTTP/1.1 200 OK \r\n\r\n");
  char filename[51] = ".";
  strcat(filename, uri);
  FILE *head = fopen("head.html", "r");
  if (!head) {
    printf("Head not found\n");
    exit(1);
  }

  FILE * tail = fopen("tail.html", "r");
  if (!tail) {
    printf("Tail not found\n");
    exit(1);
  }

  char line[1025];

  while (fgets(line, 1024, head))
    fprintf(clientfp, "%s", line);
  fclose (head);

  if (strstr(uri, ".md"))
    fprintf(
        clientfp,
        "%s",
        mdToHtml(filename));

  else if (strstr(uri, ".css"))
    fprintf(clientfp, "%s", rawFile(filename));

  while (fgets(line, 1024, tail))
    fprintf(clientfp, "%s", line);
  fclose (tail);
};

void startServer(const char * port)
{
    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &res) != 0)
    {
        printf("getaddrinfo() error\n");
        exit(1);
    }

    for (p = res; p != NULL; p = p->ai_next)
    {
        int opt = 1;
        listenfd = socket (p->ai_family, p->ai_socktype, 0);
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (listenfd == -1)
        {
            printf("listenfd == -1 ?\n");
        }

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) 
            break;
    }
    if (!p)
    {
        printf("Error bind() or socket()\n");
    }
    freeaddrinfo(res);

    /* Listen for incoming connections */
    if (listen(listenfd, 100000) != 0)
    {
        printf("Error listening\n");
    }

}



/* Client connection */
void respond(int n)
{
    int rcvd, fd, bytes_read;
    char *ptr;

    buf = malloc(BUFSZ);
    rcvd = recv(clients[n], buf, BUFSZ, 0);

    /* Receive error */
    if (rcvd < 0)
    {
        printf("recv() err\n");
    } else if (rcvd == 0)
    {
        printf("Client disconnected unexpectedly\n");
    } else // Mesage recvd
    {
        printf("%s\n", buf);
        buf[rcvd] = '\0';

        method = strtok(buf, " \t\r\n");
        uri = strtok(NULL,   " \t");
        prot = strtok(NULL,  " \t\r\n");

        printf("\x1b[33m  + [%s] %s\x1b[0m\n", method, uri);


        if ((qs = strchr(uri, '?')))
        {
            *qs++ = '\0';
        } else {
            qs = uri -1;
        }
        header_t *h = reqhdr;
        char *t, *tt;

        while (h < reqhdr + 16)
        {
            char *k, *v, *t;

            k = strtok(NULL, "\r\n: \t");
            if (!k) break;

            v = strtok(NULL, "\r\n");
            while (*v && *v == ' ') v++;

            h->name = k;
            h->value = v;
            h++;
            printf("[H] %s: %s\n", k, v);
            t = v + 1 + strlen(v);
            if (t[1] == '\r' && t[2] == '\n') break;
        }
        t++; /* Now the *t shall be at the beginning of the payload */
        tt = request_header("Content-Length");
        
        payload = t;
        payload_size = tt ? atol(tt) : (rcvd - (t - buf));

        /* Bind clientfd to stdout */
        clientfd = clients[n];

       // close(clientfd);


        clientfp = fdopen(clientfd, "w");
        /* Call router */
        route();
        /* Tidy up */
        fflush(clientfp);
        shutdown(clientfd, SHUT_WR);
        close(clientfd);
    }
    clients[n] = -1;
}

void serve(const char *port)
{
    struct sockaddr_in  client_addr;
    socklen_t addr_len;
    char c;
    int slot = 0;
    for (int i = 0; i < CONNMAX; i++)
        clients[i] = -1;
    startServer(port);
    /* Ignore SIGCHLD to avoid zombies */
    signal(SIGCHLD, SIG_IGN);

    /* Accept connections */
    while (1)
    {
        addr_len = sizeof(client_addr);
        clients[slot] = accept (listenfd, (struct sockaddr *) &client_addr, &addr_len);

        if (clients[slot] < 0)
        {
            printf("accept() error:\n");
        } else {
            client_count++;
            if (fork() == 0)
            {
                printf("respond()\n");

                respond (slot);
                exit(0);
            } else {

            }
        }
    }

}

int main()
{
  printf("hi\n");
  serve("8080");
}

/*
void route() {
    ROUTE_START()
    
    ROUTE_GET("/")
    {
        fprintf(clientfp, "HTTP/1.1 200 OK \r\n\r\n");
        printf("Opening file\n");
        FILE *stream = 
            fopen("dist/index.html", "r");

        if (!stream)
        {
            printf("Error loading index.html");
        }
        char c;
        do
        {
            c = fgetc(stream);
            if (feof(stream))
                break;
            fputc(c, clientfp);
            printf("%c", c);
        } while (1);
        printf("Closing\n");
        fclose(stream);
    }

    ROUTE_GET("/asd") {
        printf("asd\n");
        fprintf(clientfp, "HTTP/1.1 200 OK \r\n\r\n");
        fprintf(clientfp, "XD\r\n");
    }

    ROUTE_POST("/")
    {
            printf("HTTP/1.1 200 OK \r\n\r\n");
            printf("Wow, seems that you POSTed %d bytes. \r\n", payload_size);
            if (payload_size > 0)
                  printf("Request body: %s", payload);
    }

    ROUTE_HEAD("/")
    {
            fprintf(clientfp, "HTTP/1.1 200 OK \r\n");
            FILE *stream = fopen("dist/index.html", "r");
            if (!stream) {
                fprintf(clientfp, "Content-Type: 0\r\n");
            } else {
                fseek(stream, 0L, SEEK_END);
                size_t size = ftell(stream);
                fprintf(clientfp, "ETag: \"%zd\"\r\n", size);
                printf("asd %zd\n", size);

            }

    }

    ROUTE_END()
    printf("Client count: %d/%d\n", client_count, CONNMAX);
}
*/
