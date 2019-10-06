#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
/*	Copyright (c) 2019, Tofu von Helmholtz.
 *	All rights reserved.
 *
 *	Usage:	gcc print-links.c -o print-links
 *			./print-links -h 127.0.0.1
 */

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <getopt.h>
#include <sys/socket.h>
#include <errno.h>
#include <zconf.h>

#define ERR(format, ...) fprintf(stderr, "-> error in %s() line %d\n"format, __func__, __LINE__, ##__VA_ARGS__)
#define PRINT_ERROR_AND_RETURN(format, ...) ERR(format, ##__VA_ARGS__); \
        return(EXIT_FAILURE)

#define MAX_REQUEST_SIZE 100
#define MAX_READ_SIZE 10000
#define MAX_STRING_SIZE 2000

typedef struct _HttpContent {
	char get_http_request[MAX_REQUEST_SIZE];
	char received_data[MAX_READ_SIZE];
} HttpContent;

typedef struct _Connection {
	char *raw_host;
	int sck;
	HttpContent http_data;
} Connection;

int Read(void *usrbuf, int sck, size_t n, Connection *connection);
int ConnectToHost(Connection *connection, const void *port);
int SetupConnection(const char *host, const void *port);
int Write(void *request, int sck, size_t n);
char *GetNewLocation(char *html);
void FindHyperLinks(char *html, Connection *connection);


int main(int argc, char *argv[])
{
	int opt;
	Connection connection;

	if (argc != 3) {
		PRINT_ERROR_AND_RETURN("Usage: %s -h host\n", argv[0]);
	}

	while ((opt = getopt(argc, argv, "h:")) != -1) {
		switch (opt) { // NOLINT(hicpp-multiway-paths-covered)
			case 'h':
				connection.raw_host = optarg;
				break;
			default:
			PRINT_ERROR_AND_RETURN("(Usage: %s -h host)\n", argv[0]);
		}
	}
	printf("-> (%s)\n", connection.raw_host);
	if (ConnectToHost(&connection, "http") < 0)
		return EXIT_FAILURE;

	FindHyperLinks(connection.http_data.received_data, &connection);
	printf("(%s) <-\n\n", connection.raw_host);

	return EXIT_SUCCESS;

}


int ConnectToHost(Connection *connection, const void *port)
{
	connection->sck = SetupConnection(connection->raw_host, port);
	if (connection->sck < 0) {
		return -1;
	}

	snprintf(connection->http_data.get_http_request, MAX_REQUEST_SIZE, "GET / HTTP/1.1\r\nHost: %s\r\n\r\n",
			 connection->raw_host);

	if (Write(connection->http_data.get_http_request, connection->sck,
			  strlen(connection->http_data.get_http_request)) < 0) {
		ERR("Write error: (%s)\n", strerror(errno));
		return -1;
	}

	if (Read(connection->http_data.received_data, connection->sck, MAX_READ_SIZE, connection) < 0) {
		if (errno != 0)
			ERR("Write error: (%s)\n", strerror(errno));
		return -1;
	}

	if (strstr(connection->http_data.received_data, "HTTP/1.1 200") < 0) {
		ERR("strstr error: (HTTP/1.1 200 OK Not found)\n");
		return -1;
	}

	return 0;
}


int SetupConnection(const char *host, const void *port)
{
	struct addrinfo hints, *listp, *p;
	int status, clientfd = 0;

	memset(&hints, 0, sizeof hints); // Make sure the struct is empty
	hints.ai_family = AF_INET;          // Don't care IPv4
	hints.ai_socktype = SOCK_STREAM;    // TCP stream sockets
	hints.ai_flags = AI_CANONNAME | AI_ALL | AI_ADDRCONFIG;        // Fill in my IP for me

	status = getaddrinfo(host, port, &hints, &listp);
	if (status != 0) {
		ERR("getaddrinfo error: (%s)\n", gai_strerror(status));
		return -1;
	}

	for (p = listp; p; p = p->ai_next) {
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
			ERR("socket");
			continue; // Socket failed, try the next
		}

		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			break; // Success

		if (close(clientfd) < 0) { // Connect failed, try another
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


void FindHyperLinks(char *html, Connection *connection)
{
	char string[MAX_STRING_SIZE], *start_of_string, *end_of_string, encase;
	ssize_t size;

	if (!html)
		return;
	while ((html = strstr(html, "href="))) {

		html += sizeof("href=") - 1;
		encase = *html;

		if (encase == '\'' || encase == '"') {
			start_of_string = html;
			end_of_string = strchr(++start_of_string, encase);

			if (end_of_string != NULL) {
				size = end_of_string - start_of_string;
				if (size > 0 || size < MAX_STRING_SIZE) {
					strncpy(string, start_of_string, size);
					string[size] = '\0';
					puts(string);
				}
			}
		}
		++html;
	}
}


int Write(void *request, int sck, size_t n)
{
	size_t nleft = n;
	ssize_t nwritten;

	char *bufp = request;
	while (nleft > 0) {
		if ((nwritten = write(sck, bufp, nleft)) <= 0) {
			if (errno == EINTR)  // Interrupted by sig handler
				nwritten = 0;
			else
				return -1;
		}
		nleft -= nwritten;
		bufp += nwritten;
	}
	return n;
}


int Read(void *usrbuf, int sck, size_t n, Connection *connection)
{
	char *found_301, *found_302, *bufp;
	ssize_t nread;
	size_t nleft = n;
	int first_read_flag = 1;
	static int stop_loop = 0;
	int n_new_read;

	bufp = (char *) usrbuf;
	while (nleft > 0) {
		if ((nread = read(sck, bufp, nleft)) < 0) {
			if (errno == EINTR) // Interrupted by sig handler
				nread = 0;
			else
				return -1;
		} else if (nread == 0)
			break;
		nleft -= nread;
		bufp += nread;

		if (first_read_flag) {
			first_read_flag = 0;

			if (stop_loop) {
				ERR("Read error: Could not recover HTTP/1.1 302 or HTTP/1.1 301\n");
				return -1;
			}
			if (strstr(usrbuf, "HTTP/1.1 503")) {
				ERR("HTTP/1.1 503\n");
				return -1;
			}

			found_302 = strstr(usrbuf, "HTTP/1.1 302");
			found_301 = strstr(usrbuf, "HTTP/1.1 301");
			if (found_302 || found_301) {
				connection->raw_host = GetNewLocation(usrbuf);
				if (connection->raw_host == NULL) {
					ERR("GetNewLocation: error\n");
					return -1;
				} else {
					stop_loop = 1;
					n_new_read = ConnectToHost(connection, "http");
				}
				return n_new_read;
			}
		}
	}
	return (n - nleft); // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
}


char *GetNewLocation(char *html)
{
	char *link = strstr(html, "://");

	if (!link)
		return NULL;

	link += sizeof("://") - 1;

	char *link_end = strchr(link + 1, '/');
	*link_end = '\0';

	return link;
}


#pragma clang diagnostic pop