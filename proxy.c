#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1126400
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

static char cache[MAX_CACHE_SIZE];
static int cache_index;

/* Data structures for LRU */
#define MOD 3

typedef struct node {
    struct node *prev, *next;
    size_t len;
    int hashnum;
    char *buf;
} node;
typedef struct obj {
    char name[MAXLINE];
    struct node *cache;
    struct obj *next;
} obj;
struct node *head, *tail;
struct obj *hashmap[MOD];

int hash_func(char *str) {
    int sum = 0;
    for (int i = strlen(str) - 1; i >= 0; i--) {
        sum += str[i];
    }
    return sum % MOD;
}


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

void start_proxy(int connfd) {
        int clientfd;
        int hashnum;
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

        hashnum = hash_func(uri);
        if (hashmap[hashnum] != NULL) {
            struct obj *cur = hashmap[hashnum];
            while (cur != NULL && strcmp((const char *)cur->name, uri)) {
                cur = cur->next;
            }
            if (cur != NULL) {
                if (cur->cache) {
                    Rio_writen(connfd, cur->cache->buf, cur->cache->len);

                    if (cur->cache->next != NULL)
                        cur->cache->next->prev = cur->cache->prev;
                    if (cur->cache->prev != NULL)
                        cur->cache->prev->next = cur->cache->next;
                    if (head != cur->cache)
                        cur->cache->next = head;
                    cur->cache->prev = NULL;
                    if (head != NULL)
                        head->prev = cur->cache;
                    else
                        tail = cur->cache;
                    head = cur->cache;

                    return;
                }
            }
        }

        sprintf(host_hdr, "Host: %s:%s\r\n", host, port);

        clientfd = Open_clientfd(host, port);

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

        /*
        char resp_buf[MAXLINE];
        rio_t s_rio;
        Rio_readinitb(&s_rio, clientfd);
        while (Rio_readlineb(&s_rio, resp_buf, MAXLINE)) {
            Rio_writen(connfd, resp_buf, strlen(resp_buf));
        }
        */
        char resp_cache[MAX_OBJECT_SIZE + 5];
        size_t size = Rio_readn(clientfd, resp_cache, MAX_OBJECT_SIZE);
        Rio_writen(connfd, resp_cache, size);

        struct obj *cur = hashmap[hashnum];
        if (cur != NULL) {
            while (strcmp((const char *)cur->name, uri) && cur->next != NULL) {
                cur = cur->next;
            }
            if (cur->next == NULL) {
                cur->next = (struct obj *)malloc(sizeof(obj));
                cur = cur->next;
                strcpy(cur->name, uri);
                cur->next = NULL;
            }
        } else {
            hashmap[hashnum] = (struct obj *)malloc(sizeof(obj));
            cur = hashmap[hashnum];
            strcpy(cur->name, uri);
            cur->next = NULL;
        }

        if (cache_index < MOD) {
            cur->cache = (struct node *)malloc(sizeof(node));
            cur->cache->hashnum = hashnum;
            cur->cache->buf = cache + cache_index * MAX_OBJECT_SIZE;
            cache_index++;
            memcpy(cur->cache->buf, resp_cache, size);
            cur->cache->len = size;

            cur->cache->next = head;
            cur->cache->prev = NULL;
            if (head != NULL)
                head->prev = cur->cache;
            else
                tail = cur->cache;
            head = cur->cache;
        } else {
            cur->cache = (struct node *)malloc(sizeof(node));
            cur->cache->hashnum = hashnum;
            cur->cache->buf = tail->buf;
            memcpy(cur->cache->buf, resp_cache, size);
            cur->cache->len = size;

            struct obj *temp = hashmap[tail->hashnum];
            if (temp->cache == tail) {
                hashmap[tail->hashnum]->cache = NULL;
            } else {
                while (temp->next != NULL && temp->next->cache != tail) 
                    temp = temp->next;
                if (temp->next != NULL) {
                    temp->next->cache = NULL;
                }
            }
            tail = tail->prev;
            free(tail->next);
            tail->next = NULL;

            cur->cache->next = head;
            cur->cache->prev = NULL;
            if (head != NULL)
                head->prev = cur->cache;
            head = cur->cache;
        }
}

void *wrapper(void *connfd_p) {
    int connfd = *(int *)connfd_p;
    start_proxy(connfd);
    Close(connfd);
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
        
        pthread_t phd;
        pthread_create(&phd, NULL, wrapper, &connfd);
        pthread_detach(phd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}
