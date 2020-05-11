#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"

/*	isTofu <mwalk762@mtroyal.ca>
 * 
 *	Usage:	gcc fetch_headers.c -o fetch_headers
 *			./fetch_headers -h www.website.com -p port
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>

#define DEBUG 0

#define ERR(format, ...) fprintf(stderr, "-> error in %s() line %d\n" format, __func__, __LINE__, ##__VA_ARGS__)
#define PRINT_ERROR_AND_RETURN(format, ...) \
    ERR(format, ##__VA_ARGS__);             \
    return EXIT_FAILURE
#define PRINT_ERROR_AND_EXIT(format, ...) \
    ERR(format, ##__VA_ARGS__);           \
    exit EXIT_FAILURE

#define MAX_REQUEST_SIZE 50
#define MAX_READ_SIZE 100000

#define REQ_GET_HEADER_BODY_DONT_CLOSE "GET / HTTP/1.1\r\nHost: %s:%d\r\n\r\n" /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_DONT_CLOSE "HEAD / HTTP/1.1\r\nHost: %s:%d\r\n\r\n"     /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_CLOSE "HEAD / HTTP/1.0\r\n\r\n"
#define REQ_GET_BODY_CLOSE "GET /\r\n\r\n"

typedef struct _HttpContent
{
    char get_http_request[MAX_REQUEST_SIZE];
    char received_data[MAX_READ_SIZE];
} HttpContent;

typedef struct _Connection
{
    char *raw_host;
    char *port_string;
    int port_numeric;
    int sck;
    HttpContent http_data;
    struct timeval time;
} Connection;

void InitData(Connection *con);
int GetHTTPContent(Connection *con, size_t n);
int SendGetRequest(Connection *con, const char *request, ...);
int EstablishConnection(Connection *con);
int Write(void *request, int sck, size_t n);
ssize_t Read(int fd, void *usrbuf, size_t n);

int main(int argc, char *argv[])
{
    int opt;
    Connection connection;

    connection.time.tv_sec = 2;
    connection.time.tv_usec = 0;

    if (argc != 5)
    {
        PRINT_ERROR_AND_RETURN("Usage: %s -h host -p port\n", argv[0]);
    }

    while ((opt = getopt(argc, argv, "h:p:t:")) != -1)
    {
        switch (opt)
        { // NOLINT(hicpp-multiway-paths-covered)
        case 'h':
            connection.raw_host = optarg;
            break;
        case 'p':
            if ((connection.port_numeric = (int)strtol(optarg, NULL, 10)) == 80)
                connection.port_string = "http";
            else
            {
                PRINT_ERROR_AND_RETURN("(Usage: %s -h host -p port)\n", argv[0]);
            }
            break;
        case 't':
            break;Michael S. Walker %s -h host -p port)\n", argv[0]);
        }
    }

    printf("Current Host: -> [%s]\n", connection.raw_host);
    InitData(&connection);
    GetHTTPContent(&connection, MAX_READ_SIZE);
    puts(connection.http_data.received_data);

    return 0;
}

void InitData(Connection *con)
{
    memset(&con->http_data.get_http_request, '\0', MAX_REQUEST_SIZE - 1);
    memset(&con->http_data.received_data, '\0', MAX_REQUEST_SIZE - 1);
}

int GetHTTPContent(Connection *con, size_t n)
{
    size_t nleft = n;
    ssize_t nread;

    if (con->raw_host == NULL || SendGetRequest(con, REQ_GET_BODY_CLOSE, con->raw_host, con->port_numeric) < 0)
        return 0;

    do
    {
        nread = Read(con->sck, &con->http_data.received_data[n - nleft], nleft);
        nleft -= nread;
    } while (nleft > 0 && strstr(&con->http_data.received_data[0], "\r\n\r\n") == NULL);
}

int SendGetRequest(Connection *con, const char *request, ...)
{
    va_list ap;
    int status = -1; /* fail */

    con->sck = EstablishConnection(con); /* connect to host */
    if (con->sck < 0)
        goto fail_sck;

    va_start(ap, request);
    if ((vsnprintf(con->http_data.get_http_request, MAX_REQUEST_SIZE, request, ap)) < 0)
    { /* Build request string */
        ERR("vsnprintf error:\n");
        goto fail_va;
    }

    if (Write(con->http_data.get_http_request, con->sck, strlen(con->http_data.get_http_request)) < 0)
    { /* send request to host */
        ERR("Write error: (%s)\n", strerror(errno));
        close(con->sck);
        goto fail_va;
    }
    status = 0; /* success */

fail_va:
    va_end(ap);
fail_sck:
    return status;
}

int EstablishConnection(Connection *con)
{
    struct addrinfo hints, *listp, *p = NULL;
    int status, clientfd = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME | AI_ALL | AI_ADDRCONFIG;

    status = getaddrinfo(con->raw_host, con->port_string, &hints, &listp);
    if (status != 0)
    {
        ERR("getaddrinfo error: (%s)\n", gai_strerror(status));
        goto fail;
    }

    for (p = listp; p; p = p->ai_next)
    {
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; /* Socket failed, try the next */
        if (setsockopt(clientfd, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *)&con->time, sizeof(struct timeval)) < 0)
            goto fail_closefd_freeaddr;
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
            goto success; /* Success */
        if (close(clientfd) < 0)
            goto fail_freeaddr; /* Connect failed, try another */
    }
    ERR("all connects failed");

fail_closefd_freeaddr:
    close(clientfd);
success:
fail_freeaddr:
    freeaddrinfo(listp);
fail:
    return clientfd;
}

int Write(void *request, int sck, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten;

    char *bufp = request;
    while (nleft > 0)
    {
        if ((nwritten = write(sck, bufp, nleft)) <= 0)
        {
            if (errno == EINTR) /* Interrupted by sig handler */
                nwritten = 0;
            else
                return -1;
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}

ssize_t Read(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0)
    {
        if ((nread = read(fd, bufp, nleft)) < 0)
            return -1;
        else if (nread == 0)
            break;
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft);
}