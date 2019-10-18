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
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>

#define ERR(format, ...) fprintf(stderr, "-> error in %s() line %d\n"format, __func__, __LINE__, ##__VA_ARGS__)
#define PRINT_ERROR_AND_RETURN(format, ...) ERR(format, ##__VA_ARGS__); \
        return(EXIT_FAILURE)
#define PRINT_ERROR_AND_EXIT(format, ...) ERR(format, ##__VA_ARGS__); \
        exit(EXIT_FAILURE)

#define MAX_REQUEST_SIZE 50
#define MAX_READ_SIZE 100000
#define MAX_LINK_LEN 250
#define MAX_LINKS 250

#define REQ_GET_HEADER_BODY_DONT_CLOSE "GET / HTTP/1.1\r\nHost: %s:%d\r\n\r\n" /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_DONT_CLOSE "HEAD / HTTP/1.1\r\nHost: %s:%d\r\n\r\n" /* arg1 = host, arg2 = port */
#define REQ_GET_HEADER_CLOSE "HEAD / HTTP/1.0\r\n\r\n"
#define REQ_GET_BODY_CLOSE "GET /\r\n\r\n"

struct node {
	char *key;
	int val;
	struct node *next;
};

struct table {
	int size;
	struct node **list;
};

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
	struct table *hash;
	int n_links;
} Connection;

int HashLookUp(struct table *t, char *key);
void HashInsert(struct table *t, char *key, int val);
int HashCode(struct table *t, const char *key);
struct table *HashCreateTable(int size);
int SendGetRequest(Connection *con, const char *request, ...);
int Write(void *request, int sck, size_t n);
ssize_t Read(int fd, void *usrbuf, size_t n);
int EstablishConnection(Connection *con);
int ReceiveReply(Connection *con);

void FindHyperLinks(char *html, char **remote_links, char **local_links, size_t *n_remote, size_t *n_local);
void CrawlHosts(Connection *con);
char *GetNewLocation(char *html);


int main(int argc, char *argv[])
{
	int opt;
	Connection connection;
	connection.time.tv_sec = 2;
	connection.time.tv_usec = 0;
	connection.hash = HashCreateTable(MAX_LINKS);
	connection.n_links = 0;

	if (argc != 5) {
		PRINT_ERROR_AND_RETURN("Usage: %s -h host -p port\n", argv[0]);
	}

	while ((opt = getopt(argc, argv, "h:p:t:")) != -1) {    //Todo: add verbose feature, also add time in miliseconds.
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
	return 0;
}


void CrawlHosts(Connection *con)
{
	size_t n_external_links, n_local_links;

	printf("Current Host: -> [%s]\n", con->raw_host);
	memset(&con->http_data.get_http_request, '\0', MAX_REQUEST_SIZE - 1);
	memset(&con->http_data.received_data, '\0', MAX_REQUEST_SIZE - 1);

	if (con->raw_host != NULL && SendGetRequest(con, REQ_GET_HEADER_BODY_DONT_CLOSE, con->raw_host, con->port_numeric) >= 0 &&
		ReceiveReply(con) >= 0) {
		char *external_links[MAX_LINK_LEN], *local_links[MAX_LINK_LEN];

		n_external_links = n_local_links = 0;
		FindHyperLinks(con->http_data.received_data, external_links, local_links, &n_external_links, &n_local_links);

		puts("========external links===========");
		for (size_t i = 0; i < n_external_links; i++) {
			char *new_host = GetNewLocation(external_links[i]);
			if (HashLookUp(con->hash, new_host) == -1) {
				printf("Hash host: %s\n", new_host);
				HashInsert(con->hash, new_host, con->n_links);
				con->raw_host = new_host;
				con->n_links++;
				CrawlHosts(con);
			}
			puts(external_links[i]);
			free(external_links[i]);
		}

		puts("\n========internal links===========");
		for (size_t i = 0; i < n_local_links; i++) {
			puts(local_links[i]);
			free(local_links[i]);
		}
	} else if (strstr(con->http_data.received_data, "HTTP/1.1 302")) { /* check for URL redirection */
		con->raw_host = GetNewLocation(con->http_data.received_data);
		if (SendGetRequest(con, REQ_GET_HEADER_CLOSE) >= 0 && strstr(con->http_data.received_data, "\"HTTP/1.1 200 OK"))
			CrawlHosts(con);
	}
}


int SendGetRequest(Connection *con, const char *request, ...)
{
	va_list ap;
	int status = -1; /* fail */

	con->sck = EstablishConnection(con); /* connect to host */
	if (con->sck < 0)
		goto fail_sck;

	va_start(ap, request);
	if ((vsnprintf(con->http_data.get_http_request, MAX_REQUEST_SIZE, request, ap)) < 0) { /* Build request string */
		ERR("vsnprintf error:\n");
		goto fail_va;
	}

	if (Write(con->http_data.get_http_request, con->sck, strlen(con->http_data.get_http_request)) < 0) { /* send request to host */
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


int ReceiveReply(Connection *con)
{
	int n_bytes;
	char *temp_buf = malloc(MAX_READ_SIZE + 10);
	n_bytes = read(con->sck, temp_buf, MAX_READ_SIZE);
	puts(temp_buf);
	if (errno != 0) {
		ERR("read failed: (%s)\n", strerror(errno));
		close(con->sck);
	}

	return -1; /* fail */
}


void FindHyperLinks(char *html, char **remote_links, char **local_links, size_t *n_remote, size_t *n_local)
{
	char temp[MAX_LINK_LEN], *start_of_link, *end_of_link, encase;
	ssize_t size;

	if (!html) {
		PRINT_ERROR_AND_EXIT("error char *html was NULL\n");
	}

	while ((*n_remote < MAX_LINKS) && ((html = strstr(html, "href=")) != NULL)) {
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
					if (local_links[*n_local] == NULL)
						goto fail;

					strncpy(local_links[*n_local], start_of_link, size);
					local_links[*n_local][size] = '\0';
					*n_local += 1;

				} else { /* temp is a uri */

					remote_links[*n_remote] = malloc(size + 1);
					if (remote_links[*n_remote] == NULL)
						goto fail;

					strncpy(remote_links[*n_remote], start_of_link, size);
					remote_links[*n_remote][size] = '\0';
					*n_remote += 1;
				}
			}
		}
		++html;
	}
	return; /* success */

fail:
	for (size_t i = 0; i < *n_remote; i++)
		free(remote_links[*n_remote]);
	for (size_t i = 0; i < *n_local; i++)
		free(local_links[*n_local]);
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
	if (status != 0) {
		ERR("getaddrinfo error: (%s)\n", gai_strerror(status));
		goto fail;
	}

	for (p = listp; p; p = p->ai_next) {
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
			continue; /* Socket failed, try the next */
		}
		if (setsockopt(clientfd, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *) &con->time, sizeof(struct timeval)) < 0)
			goto fail_closefd_freeaddr;
		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			goto success; /* Success */

		if (close(clientfd) < 0) { /* Connect failed, try another */
			goto fail_freeaddr;
		}
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


ssize_t Read(int fd, void *usrbuf, size_t n)
{
	size_t nleft = n;
	ssize_t nread;
	char *bufp = usrbuf;

	while (nleft > 0) {
		if ((nread = read(fd, bufp, nleft)) < 0) {
			return -1;
		} else if (nread == 0)
			break;
		nleft -= nread;
		bufp += nread;
	}
	return (n - nleft);
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


struct table *HashCreateTable(int size)
{
	struct table *t = (struct table *) malloc(sizeof(struct table));
	t->size = size;
	t->list = (struct node **) malloc(sizeof(struct node *) * size);
	int i;
	for (i = 0; i < size; i++)
		t->list[i] = NULL;
	return t;
}


int HashCode(struct table *t, const char *key)
{
	int n_key = (int) strlen(key);
	n_key += key[n_key - 1];
	n_key ^= key[0];

	if (n_key < 0)
		return -(n_key % t->size);
	return n_key % t->size;
}


void HashInsert(struct table *t, char *key, int val)
{
	int pos = HashCode(t, key);
	struct node *list = t->list[pos];
	struct node *newNode = (struct node *) malloc(sizeof(struct node));
	struct node *temp = list;
	while (temp) {
		if ((strncmp(temp->key, key, strlen(temp->key)) == 0)) {
			temp->val = val;
			return;
		}
		temp = temp->next;
	}
	newNode->key = key;
	newNode->val = val;
	newNode->next = list;
	t->list[pos] = newNode;
}


int HashLookUp(struct table *t, char *key)
{
	int pos = HashCode(t, key);
	struct node *list = t->list[pos];
	struct node *temp = list;
	while (temp) {
		if ((strncmp(temp->key, key, strlen(temp->key)) == 0)) {
			return temp->val;
		}
		temp = temp->next;
	}
	return -1;
}


#pragma clang diagnostic pop