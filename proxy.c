#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE];

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
}

void parse_url(char *url, char *host, char *port, char *uri) {
    int i, j;
    for (i = 7, j = 0; url[i] != ':'; i++, j++) {
        host[j] = url[i];
    }
    host[j] = 0; i++;
    for (j = 0; url[i] != '/'; i++, j++) {
        port[j] = url[i];
    }
    port[j] = 0;
    for (j = 0; url[i]; i++, j++) {
        uri[j] = url[i];
    }
    uri[j] = 0;
}

void transfer(int clientfd, char *uri, char *host_hdr) {
        char transfer_buf[MAXLINE];
        sprintf(transfer_buf, "GET %s HTTP/1.0\r\n", uri);
        Rio_writen(clientfd, transfer_buf, strlen(transfer_buf));
        sprintf(transfer_buf, "%s", host_hdr);
        Rio_writen(clientfd, transfer_buf, strlen(transfer_buf));
        sprintf(transfer_buf, "%s", user_agent_hdr);
        Rio_writen(clientfd, transfer_buf, strlen(transfer_buf));
        sprintf(transfer_buf, "%s", conn_hdr);
        Rio_writen(clientfd, transfer_buf, strlen(transfer_buf));
        sprintf(transfer_buf, "%s\r\n", proxy_conn_hdr);
        Rio_writen(clientfd, transfer_buf, strlen(transfer_buf));
}

void start_proxy(int connfd) {
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char host_hdr[MAXLINE];
        rio_t rio;

        Rio_readinitb(&rio, connfd);
        if (!Rio_readlineb(&rio, buf, MAXLINE)) return;
        printf("%s", buf);

        sscanf(buf, "%s %s %s", method, url, version);
        if (strcasecmp(method, "GET")) {
            clienterror(connfd, method, "501", "Not Implemented", "Proxy does not implement this method");
            return;
        }

        char host[MAXLINE], uri[MAXLINE], port[MAXLINE];
        parse_url(url, host, port, uri);

        sprintf(host_hdr, "Host: %s:%s\r\n", host, port);

        clientfd = Open_clientfd(host, port);

        transfer(clientfd, uri, host_hdr);

        /*
        char resp_buf[MAXLINE];
        rio_t s_rio;
        Rio_readinitb(&s_rio, clientfd);
        while (Rio_readlineb(&s_rio, resp_buf, MAXLINE)) {
            Rio_writen(connfd, resp_buf, strlen(resp_buf));
        }
        */
        char resp_cache[MAX_CACHE_SIZE];
        size_t size = Rio_readn(clientfd, resp_cache, MAX_OBJECT_SIZE);
        Rio_writen(connfd, resp_cache, size);
        Close(clientfd);
}

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA* )&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        start_proxy(connfd);

        Close(connfd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}
