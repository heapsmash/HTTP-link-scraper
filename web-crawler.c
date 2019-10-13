#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"

/*	Copyright (c) 2019, Michael S. Walker <sigmatau@heapsmash.com>
 *	All rights reserved.
 *
 *	Usage:	gcc print-links.c -o print-links
 *			./print-links -h 127.0.0.1 -p port
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#define ERR(format, ...) fprintf(stderr, "-> error in %s() line %d\n"format, __func__, __LINE__, ##__VA_ARGS__)
#define PRINT_ERROR_AND_RETURN(format, ...) ERR(format, ##__VA_ARGS__); \
        return(EXIT_FAILURE)
#define PRINT_ERROR_AND_EXIT(format, ...) ERR(format, ##__VA_ARGS__); \
        exit(EXIT_FAILURE)

#define MAX_REQUEST_SIZE 1000
#define MAX_READ_SIZE 1000000 /* 1mb */
#define MAX_LINK_LEN 2000
#define MAX_LINKS 1000

#define REQ_GET_HEADER_BODY_DONT_CLOSE "GET / HTTP/1.1\r\nHost: %s:%d\r\n\r\n" /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_DONT_CLOSE "HEAD / HTTP/1.1\r\nHost: %s:%d\r\n\r\n" /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_CLOSE "HEAD / HTTP/1.0\r\n\r\n"
#define REQ_GET_BODY_CLOSE "GET /\r\n\r\n"

typedef struct _HttpContent {
	char get_http_request[MAX_REQUEST_SIZE];
	char received_data[MAX_READ_SIZE];
} HttpContent;

typedef struct _Connection {
	char *raw_host;
	char *port_string;
	int port_numeric;
	int sck;
	HttpContent http_data;
	struct timeval time;
} Connection;

int SendGetRequest(Connection *con, const char *request, ...);
int ReceiveReply(Connection *con);

int EstablishConnection(const char *host, const void *port);
int Write(void *request, int sck, size_t n);

void FindHyperLinks(char *html, char **remote_links, char **local_links, size_t *n_remote, size_t *n_local);
void CrawlHosts(Connection *con);
char *GetNewLocation(char *html);


int main(int argc, char *argv[])
{
	int opt;
	Connection connection;
	connection.time.tv_sec = 2;
	connection.time.tv_usec = 0;

	if (argc != 5) {
		PRINT_ERROR_AND_RETURN("Usage: %s -h host -p port\n", argv[0]);
	}

	while ((opt = getopt(argc, argv, "h:p:t:")) != -1) {
		switch (opt) { // NOLINT(hicpp-multiway-paths-covered)
			case 'h':
				connection.raw_host = optarg;
				break;
			case 'p':
				if ((connection.port_numeric = (int) strtol(optarg, NULL, 10)) == 80)
					connection.port_string = "http";
				else {
					PRINT_ERROR_AND_RETURN("(Usage: %s -h host -p port)\n", argv[0]);
				}
				break;
			case 't':
				break;
			default:
			PRINT_ERROR_AND_RETURN("(Usage: %s -h host -p port)\n", argv[0]);
		}
	}

	CrawlHosts(&connection);
	return EXIT_SUCCESS;
}


void CrawlHosts(Connection *con)
{
	char *external_links[MAX_LINK_LEN], *local_links[MAX_LINK_LEN];
	size_t n_external_links, n_local_links, i;

	if (con->raw_host == NULL)
		return;

	if (SendGetRequest(con, REQ_GET_HEADER_CLOSE) < 0)
		con->raw_host = NULL;
	else if (ReceiveReply(con) < 0 &&
			 (strstr(con->http_data.received_data, "HTTP/1.1 302") || strstr(con->http_data.received_data, "HTTP/1.1 301"))) {
		con->raw_host = GetNewLocation(con->http_data.received_data);
		CrawlHosts(con);
	}

	if (con->raw_host != NULL) {
		SendGetRequest(con, REQ_GET_BODY_CLOSE);
		ReceiveReply(con);
		FindHyperLinks(con->http_data.received_data, external_links, local_links, &n_external_links, &n_local_links);

		puts("========external links===========");
		for (i = 0; i < n_external_links; i++) {
			puts(external_links[i]);
		}

		puts("\n========internal links===========");
		for (i = 0; i < n_local_links; i++) {
			puts(local_links[i]);
		}
	}
}


void FindHyperLinks(char *html, char **remote_links, char **local_links, size_t *n_remote, size_t *n_local)
{
	char temp[MAX_LINK_LEN], *start_of_link, *end_of_link, encase;
	ssize_t size;

	*n_local = *n_remote = 0;
	if (!html) {
		PRINT_ERROR_AND_EXIT("error char *html was NULL\n");
	}

	while (*n_remote < MAX_LINKS && (html = strstr(html, "href="))) {
		html += sizeof("href=") - 1;
		encase = *html;

		if (encase == '\'' || encase == '"') {
			start_of_link = html;
			end_of_link = strchr(++start_of_link, encase);
			size = end_of_link - start_of_link;

			if (end_of_link != NULL && size > 0 && size < MAX_LINK_LEN) {
				strncpy(temp, start_of_link, size); /* store the new link */
				temp[size] = '\0';

				if (GetNewLocation(temp) == NULL) { /* temp is a local link */
					local_links[*n_local] = malloc(size + 1);
					strncpy(local_links[*n_local], start_of_link, size);
					local_links[*n_local][size] = '\0';
					*n_local += 1;

				} else { /* temp is a uri */
					remote_links[*n_remote] = malloc(size + 1);
					strncpy(remote_links[*n_remote], start_of_link, size);
					remote_links[*n_remote][size] = '\0';
					*n_remote += 1;
				}
			}
		}
		++html;
	}
}


int SendGetRequest(Connection *con, const char *request, ...)
{
	va_list ap;

	memset(&con->http_data.get_http_request, '\0', MAX_REQUEST_SIZE - 1);
	memset(&con->http_data.received_data, '\0', MAX_REQUEST_SIZE - 1);

	con->sck = EstablishConnection(con->raw_host, con->port_string);
	if (con->sck < 0)
		return -1;

	va_start(ap, request);  /* Build request string */
	if ((vsnprintf(con->http_data.get_http_request, MAX_REQUEST_SIZE, request, ap)) < 0) {
		va_end(ap);
		PRINT_ERROR_AND_EXIT("vsnprintf error:\n");
	}
	va_end(ap);

	if (Write(con->http_data.get_http_request, con->sck, /* send request to host */
			  strlen(con->http_data.get_http_request)) < 0) {
		PRINT_ERROR_AND_EXIT("Write error: (%s)\n", strerror(errno));
	}

	return 0;
}


int ReceiveReply(Connection *con)
{
	if (read(con->sck, con->http_data.received_data, MAX_READ_SIZE) < 0) { /* get response */
		if (errno == EINTR)    /* interrupted by sig handler call again */
			return ReceiveReply(con);
		if (errno != 0) {
			PRINT_ERROR_AND_EXIT("Read error: (%s)\n", strerror(errno));
		} else {
			PRINT_ERROR_AND_EXIT("Read error:");
		}
	}

	if (strstr(con->http_data.received_data, "HTTP/1.1 200") < 0) {
		ERR("strstr error: (HTTP/1.1 200 OK Not found)\n");
		return -1;
	}

	return 0;
}


int EstablishConnection(const char *host, const void *port)
{
	struct addrinfo hints, *listp, *p;
	int status, clientfd = 0;

	struct timeval time = {
			2,
			0
	};

	memset(&hints, 0, sizeof hints); /* Make sure the struct is empty */
	hints.ai_family = AF_INET;          /* Don't care IPv4 */
	hints.ai_socktype = SOCK_STREAM;    /* TCP stream sockets */
	hints.ai_flags = AI_CANONNAME | AI_ALL | AI_ADDRCONFIG;

	status = getaddrinfo(host, port, &hints, &listp);
	if (status != 0) {
		ERR("getaddrinfo error: (%s)\n", gai_strerror(status));
		return -1;
	}

	for (p = listp; p; p = p->ai_next) {
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
			ERR("socket");
			continue; /* Socket failed, try the next */
		}

		setsockopt(clientfd, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *) &time, sizeof(struct timeval));

		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			break; /* Success */

		if (close(clientfd) < 0) { /* Connect failed, try another */
			ERR("close failed: (%s)\n", strerror(errno));
			return -1;
		}
	}

	freeaddrinfo(listp);
	if (!p) {
		ERR("all connects failed");
		return -1;
	} else
		return clientfd;
}


int Write(void *request, int sck, size_t n)
{
	size_t nleft = n;
	ssize_t nwritten;

	char *bufp = request;
	while (nleft > 0) {
		if ((nwritten = write(sck, bufp, nleft)) <= 0) {
			if (errno == EINTR)  /* Interrupted by sig handler */
				nwritten = 0;
			else
				return -1;
		}
		nleft -= nwritten;
		bufp += nwritten;
	}
	return n;
}


char *GetNewLocation(char *html)
{
	char *link = strstr(html, "://"); /* no https */

	if (!link)
		return NULL;

	link += sizeof("://") - 1;

	char *link_end = strchr(link + 1, '/');
	*link_end = '\0';

	return link;
}


#pragma clang diagnostic pop