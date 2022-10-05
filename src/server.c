#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include "membuf.h"
#include "server.h"
#include "md.h"

void route();

void serverInit(Server *s) {
    s->reqhdr->name = "\0";
    s->reqhdr->value = "\0";
}


/* Get request header */
char *request_header(Server *s, const char *name)
{
    header_t *h = s->reqhdr;
    while (h->name)
    {
        if (strcmp(h->name, name) == 0)
            return h->value;
        h++;
    }
    return NULL;
}


void serve(Server *s, const char *port)
{
    struct sockaddr_in  client_addr;
    socklen_t addr_len;
    char c;
    int slot = 0;
    for (int i = 0; i < CONNMAX; i++)
        s->clients[i] = -1;
    startServer(s, port);
    /* Ignore SIGCHLD to avoid zombies */
    signal(SIGCHLD, SIG_IGN);

    /* Accept connections */
    while (1)
    {
        addr_len = sizeof(client_addr);
        s->clients[slot] = accept (s->listenfd, (struct sockaddr *) &client_addr, &addr_len);

        if (s->clients[slot] < 0)
        {
            printf("accept() error:\n");
        } else {
            s->client_count++;
            if (fork() == 0)
            {
                printf("respond()\n");

                respond(s, slot);
                exit(0);
            } else {

            }
        }
    }

}

void startServer(Server *server, const char * port)
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
void respond(Server *server, int n)
{
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
        printf("!%s\n", buf.data);
        buf.data[rcvd] = '\0';

        server->method = strtok(buf.data, " \t\r\n");
        server->uri = strtok(NULL,   " \t");
        server->prot = strtok(NULL,  " \t\r\n");

        printf("\x1b[33m  + [%s] %s\x1b[0m\n", server->method, server->uri);


        if ((server->qs = strchr(server->uri, '?')))
        {
            *server->qs++ = '\0';
        } else {
            server->qs = server->uri -1;
        }
        header_t *h = server->reqhdr;
        char *t, *tt;

        while (h < server->reqhdr + 16)
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
        tt = request_header(server, "Content-Length");

        char    *payload;     // for POST
        int      payload_size;
        
        payload = t;
        payload_size = tt ? atol(tt) : (rcvd - (t - buf.data));

        /* Bind clientfd to stdout */
        server->clientfd = server->clients[n];

       // close(clientfd);


        server->clientfp = fdopen(server->clientfd, "w");
        /* Call router */
        route();
        /* Tidy up */
        fflush(server->clientfp);
        shutdown(server->clientfd, SHUT_WR);
        close(server->clientfd);
    }
    server->clients[n] = -1;
}

void cat_file(FILE* out, char *filename) {
  if (!out) {
    printf("Excepción: cat_file(): El archivo no existe o puntero NULL");
    return;
  }

  char line[LINE_MAX];

  FILE *in = fopen(filename, "r");
  
  while (fgets(line, LINE_MAX - 1, in))
    fprintf(out, "%s", line);
  fclose(in);
}

void route(Server *server) {
    printf("routing\n");
  if(strcmp(server->uri, "/") == 0) {
    fprintf(server->clientfp, "HTTP/1.1 200 OK \r\n\r\n");
    fprintf(server->clientfp, "<h1>asd</h1> \r\n\r\n");
    return;
    
  }
  printf("Uri: %s\n", server->uri);

  char filename[51] = ".";
  strcat(filename, server->uri);

  DIR *dir = opendir(filename);
  struct dirent *dent;

  if (dir) {
    fprintf(server->clientfp, "HTTP/1.1 200 OK \r\n\r\n");
    while ((dent = readdir(dir)) != NULL)  {
      if (strcmp(dent->d_name, ".") == 0 || 
          strcmp(dent->d_name, "..") == 0 ||
          (*dent->d_name == '.')) {
      } else {
        fprintf(server->clientfp, "<a href = \"%s/%s\">%s</a>\r\n",server->uri, dent->d_name, dent->d_name);
      }
    }

    closedir(dir);
    return;
  }

  FILE *file = fopen(filename, "r");
  FILE *head = NULL;
  if (file)
    fprintf(server->clientfp, "HTTP/1.1 200 OK \r\n\r\n");
  else {
    fprintf(server->clientfp, "HTTP/1.1 404 NOT FOUND \r\n\r\n");
    cat_file(server->clientfp, "404.html");

  }

  if (strstr(server->uri, ".md")){
    cat_file(server->clientfp, "head.html");

    fprintf(
        server->clientfp,
        "%s",
        mdToHtml(file)
        );

    cat_file(server->clientfp, "tail.html");

  } else if (strstr(server->uri, ".css"))
      cat_file(server->clientfp, filename);

};
