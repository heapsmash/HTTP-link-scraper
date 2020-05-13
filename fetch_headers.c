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
    {                                       \
        ERR(format, ##__VA_ARGS__);         \
        return EXIT_FAILURE;                \
    }

#define PRINT_ERROR_AND_EXIT(format, ...) \
    {                                     \
        ERR(format, ##__VA_ARGS__);       \
        exit(EXIT_FAILURE);               \
    }

#define REQ_GET_HEADER_BODY_DONT_CLOSE "GET / HTTP/1.1\r\nHost: %s:%d\r\n\r\n" /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_DONT_CLOSE "HEAD / HTTP/1.1\r\nHost: %s:%d\r\n\r\n"     /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_CLOSE "HEAD / HTTP/1.0\r\n\r\n"
#define REQ_GET_BODY_CLOSE "GET /\r\n\r\n"

typedef struct _Connection
{
    char *raw_host;
    char *port_string;
    int port_numeric;
    int sck;
    struct timeval time;
} Connection;

int GetHTTPContent(Connection *con);
int SendGetRequest(Connection *con);
int EstablishConnection(Connection *con);

int Write(void *request, int sck, size_t n);
ssize_t Read(int fd, void *usrbuf, size_t n);

int main(int argc, char *argv[])
{
    int opt;
    Connection connection;

    connection.time.tv_sec = 2; /* timeout set to 2 */
    connection.time.tv_usec = 0;

    if (argc != 5)
    {
        PRINT_ERROR_AND_RETURN("Usage: %s -h host -p port\n", argv[0]);
    }

    while ((opt = getopt(argc, argv, "h:p:t:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            connection.raw_host = optarg;
            break;
        case 'p':
            if ((connection.port_numeric = (int)strtol(optarg, NULL, 10)) == 80)
                connection.port_string = "http";
            else
            {
                PRINT_ERROR_AND_RETURN("Invalid Port (%d) \n(Usage: %s -h host -p port)\n", connection.port_numeric, argv[0]);
            }
            break;
        case 't':
            break;
        default:
            PRINT_ERROR_AND_RETURN("(Usage: %s -h host -p port)\n", argv[0]);
        }
    }

    connection.sck = EstablishConnection(&connection); /* establish socket */
    GetHTTPContent(&connection);
    close(connection.sck);

    return 0;
}

int GetHTTPContent(Connection *con)
{
    char buf[4096];

    if (con->sck < 0 || con->raw_host == NULL || SendGetRequest(con) < 0)
        return 0;

    ssize_t i = read(con->sck, buf, sizeof buf / sizeof buf[0]);
    if (i < 0) /* something went wrong */
        return 0;
    buf[i] = '\0';

    char *tmp_head = strdup(buf);
    char *head_ptr = tmp_head;

    while (*(tmp_head + 3)) /* assume not malformed */
    {
        if (*(tmp_head) == '\r' && *(tmp_head + 1) == '\n' && *(tmp_head + 2) == '\r' && *(tmp_head + 3) == '\n')
        {
            *tmp_head = '\0';
            break;
        }
        tmp_head++;
    }

    char *head = strdup(head_ptr);
    char *con_len = strstr(head, "Content-Length: ");

    long body_sz; /* get the total number of bytes in the body either from contant length or the hex value after the \r\n\r\n */
    if (con_len)
    {
        con_len = strtok(con_len, "\n");
        body_sz = strtol(strtok(con_len, "Content-Length: "), NULL, 10);
    }
    else
    {
        char *body_ptr = strstr(buf, "\r\n\r\n");
        char *content_length = strtok(body_ptr + 4, "\n");
        body_sz = strtol(content_length, NULL, 16);
    }

    char *tmp = buf;
    while (*tmp++) /* move pointer to start of body */
        ;

    size_t body_bytes_read = strlen(tmp);

    int n = body_sz - body_bytes_read; /* get the number of bytes left to read in the body */
    char *body = malloc(body_sz);

    strncpy(body, tmp, body_bytes_read);

    return 1;
}

int SendGetRequest(Connection *con)
{
    char get_http_request[50];
    int status = -1; /* fail */

    if ((snprintf(get_http_request, sizeof get_http_request, "GET / HTTP/1.1\r\nHost: %s:%d\r\n\r\n", con->raw_host, con->port_numeric)) < 0)
    { /* Build request string */
        ERR("snprintf error:\n");
        goto end;
    }

    if (Write(get_http_request, con->sck, strlen(get_http_request)) < 0)
    { /* send request to host */
        ERR("Write error: (%s)\n", strerror(errno));
        close(con->sck);
        goto end;
    }
    status = 0; /* success */

end:
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
