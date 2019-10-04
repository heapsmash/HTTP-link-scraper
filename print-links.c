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
#define MAX_READ_SIZE 5000

typedef struct _HttpContent {
	char get_http_request[MAX_REQUEST_SIZE];
	char received_data[MAX_READ_SIZE];
} HttpContent;

typedef struct _Connection {
	char *raw_host;
	int sck;
	HttpContent http_data;
} Connection;

void PrintAllSubStrings(const char *buf, const char *needle, char start_symbol, char end_symbol);
int ConnectToHost(Connection *connection, const void *port);
int SetupConnection(const char *host, const void *port);
int Write(void *request, int sck, size_t n);
int Read(void *usrbuf, int sck, size_t n);
int ValidateRequest(const char *buf);


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

	if (ConnectToHost(&connection, "http") < 0)
		return EXIT_FAILURE;

	PrintAllSubStrings(connection.http_data.received_data, "<a href=", '"', '"');

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

	if (Read(connection->http_data.received_data, connection->sck, MAX_READ_SIZE) < 0) {
		ERR("Write error: (%s)\n", strerror(errno));
		return -1;
	}

	if (ValidateRequest(connection->http_data.received_data) < 0) {
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
	hints.ai_family = AF_UNSPEC;        // Don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;    // TCP stream sockets
	hints.ai_flags = AI_PASSIVE;        // Fill in my IP for me

	if ((status = getaddrinfo(host, port, &hints, &listp)) != 0) {
		ERR("getaddrinfo error: (%s)\n", gai_strerror(status));
		return -1;
	}

	for (p = listp; p; p = p->ai_next) {
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
			continue; // Socket failed, try the next

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


int ValidateRequest(const char *buf)
{
	if (strstr(buf, "HTTP/1.1 200 OK") == NULL) {
		return -1;
	}
	return 0;
}


void PrintAllSubStrings(const char *buf, const char *needle, char start_symbol, char end_symbol)
{
	char *new_buf = strstr(buf, needle);
	while (new_buf != NULL) {
		const char *start_of_link = strchr(new_buf, start_symbol) + 1;
		const char *end_of_link = strchr(start_of_link, end_symbol);

		char link[end_of_link - start_of_link];

		strncpy(link, start_of_link, end_of_link - start_of_link);
		link[sizeof(link)] = 0;

		puts(link);

		new_buf++;
		new_buf = strstr(new_buf, needle);
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


int Read(void *usrbuf, int sck, size_t n)
{
	size_t nleft = n;
	ssize_t nread;

	char *bufp = usrbuf;
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
	}
	return (n - nleft); // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
}

