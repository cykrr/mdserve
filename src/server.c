#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <pthread.h>
#include "membuf.h"
#include "server.h"
#include "md.h"

void serverInit(Server *s, const char *port, void (*route)(const char *uri, FILE *out))
{
    s->port = port;
    s->route = route;
}


/* Get request header */
char *request_header(header_t *reqhdr, const char *name)
{
    header_t *h = reqhdr;
    while (h->name)
    {
        if (strcmp(h->name, name) == 0){
            return h->value;
        }
        h++;
    }
    return NULL;
}


void serve(Server *s)
{
    struct sockaddr_in  client_addr;
    socklen_t addr_len;
    char c;
    int slot = 0;
    for (int i = 0; i < CONNMAX; i++)
        s->clients[i] = -1;
    startServer(s);
    /* Ignore SIGCHLD to avoid zombies */
    signal(SIGCHLD, SIG_IGN);

    /* Accept connections */
    while (1)
    {
        addr_len = sizeof(client_addr);
        int client_fd = accept(s->listenfd, (struct sockaddr *) &client_addr, &addr_len);

        if (client_fd < 0)
        {
            printf("accept() error:\n");
        } else {
            int i;
            for (i = 0; i < CONNMAX; i++) {
                if (s->clients[i] == -1) {
                    s->clients[i] = client_fd;
                    break;
                }
            }
            if (i == CONNMAX) {
                printf("Max connections reached.\n");
                close(client_fd);
                continue;
            }

            s->client_count++;
            thread_args_t *args = malloc(sizeof(thread_args_t));
            args->server = s;
            args->n = i;

            pthread_t thread;
            if (pthread_create(&thread, NULL, respond, args) != 0) {
                printf("Failed to create thread\n");
                close(client_fd);
                s->clients[i] = -1;
                free(args);
            } else {
                pthread_detach(thread);
            }
        }
    }

}

void startServer(Server *server)
{
    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, server->port, &hints, &res) != 0)
    {
        printf("getaddrinfo() error\n");
        exit(1);
    }

    for (p = res; p != NULL; p = p->ai_next)
    {
        int opt = 1;
        server->listenfd = socket (p->ai_family, p->ai_socktype, 0);
        setsockopt(server->listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (server->listenfd == -1)
        {
            printf("listenfd == -1 ?\n");
        }

        if (bind(server->listenfd, p->ai_addr, p->ai_addrlen) == 0) 
            break;
    }
    if (!p)
    {
        printf("Error bind() or socket()\n");
    }
    freeaddrinfo(res);

    /* Listen for incoming connections */
    if (listen(server->listenfd, 100000) != 0)
    {
        printf("Error listening\n");
    }

}

/* Client connection */
void *respond(void *args)
{
    thread_args_t *targs = (thread_args_t*)args;
    Server *server = targs->server;
    int n = targs->n;
    free(targs);

    int rcvd, fd, bytes_read;
    char *ptr;

    struct membuffer buf = {0};
    membuf_init(&buf, BUFSZ);
    rcvd = recv(server->clients[n], buf.data, BUFSZ, 0);

    /* Receive error */
    if (rcvd < 0)
    {
        printf("recv() err\n");
    } else if (rcvd == 0)
    {
        printf("Client disconnected unexpectedly\n");
    } else // Mesage recvd
    {
        buf.data[rcvd] = '\0';

        char *saveptr;
        char *method = strtok_r(buf.data, " \t\r\n", &saveptr);
        char *uri = strtok_r(NULL,   " \t", &saveptr);
        char *prot = strtok_r(NULL,  " \t\r\n", &saveptr);

        if (!method || !uri || !prot) {
            close(server->clients[n]);
            server->clients[n] = -1;
            membuf_fini(&buf);
            return NULL;
        }

        printf("\x1b[33m  + [%s] %s\x1b[0m\n", method, uri);

        char *qs = strchr(uri, '?');
        if (qs)
        {
            *qs++ = '\0';
        } else {
            qs = uri - 1;
        }

        header_t reqhdr[17] = {0};
        header_t *h = reqhdr;
        char *t, *tt;


        
        while (h < reqhdr + 16)
        {
            char *k, *v, *t_inner;
            k = strtok_r(NULL, "\r\n: \t", &saveptr);
            if (!k) break;

            v = strtok_r(NULL, "\r\n", &saveptr);
            if (!v) break;
            while (*v && *v == ' ') v++;

            h->name = k;
            h->value = v;
            h++;
            t_inner = v + 1 + strlen(v);
            if (t_inner[1] == '\r' && t_inner[2] == '\n') break;
        }
        /* NOTE: Original logic for extracting payload start is fragile, skipping for now as it's a GET server */

        // tt = request_header(reqhdr, "Content-Length");
        // char    *payload;     // for POST
        // int      payload_size;
        
        // payload = t;
        // payload_size = tt ? atol(tt) : (rcvd - (t - buf.data));

        /* Bind clientfd to stdout */
        int clientfd = server->clients[n];

        FILE *clientfp = fdopen(clientfd, "w");
        if (clientfp) {
            /* Call router */
            server->route(uri, clientfp);
            /* Tidy up */
            fflush(clientfp);
            shutdown(clientfd, SHUT_WR);
            fclose(clientfp);
        } else {
            close(clientfd);
        }
    }
    server->clients[n] = -1;
    membuf_fini(&buf);
    return NULL;
}

