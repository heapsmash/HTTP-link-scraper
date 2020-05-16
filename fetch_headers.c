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

int EstablishConnection(const char *host, const char *service_str);
int WebRequest(const char *verb, const char *host, const char *resource,
               int (*write_cbk)(const void *, size_t, void *), void *cbk_context);
int HttpParseHeader(char *header, char **keys, char **vals, size_t max_headers);
int HttpWriteCbk(const void *data, size_t size, void *context);

typedef struct
{
    char *key;
    char *val;
} KVPAIR;

typedef struct
{
    int fd;
    int is_done;
    size_t pos;
    size_t buflen;
    char buf[BUFSIZ];
} TEXTSCK;

void textsckinit(TEXTSCK *stream, int fd)
{
    stream->fd = fd;
    stream->is_done = 0;
    stream->pos = 0;
    stream->buflen = 0;
}

int netgetc(TEXTSCK *stream)
{
    if (stream->is_done)
        return EOF;

    if (stream->pos == stream->buflen)
    {
        ssize_t nread = read(stream->fd, stream->buf, sizeof(stream->buf));
        if (nread <= 0)
            stream->is_done = 1;

        stream->pos = 0;
        stream->buflen = nread;
    }

    return stream->buf[stream->pos++];
}

char *netgets(char *str, size_t size, TEXTSCK *stream)
{
    char *s = str;
    int c = 0;

    if (stream->is_done)
        return NULL;

    for (size_t i = 0; i != size - 1 && c != '\n'; i++)
    {
        c = netgetc(stream);
        if (c == EOF)
            break;

        *s++ = c;
    }

    *s = '\0';
    return str;
}

char *ChompWS(char *str)
{
    str += strspn(str, " \t\r\n");

    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' ||
                         *end == '\r' || *end == '\n'))
        end--;

    end[1] = '\0';
    return str;
}

int main(int argc, char *argv[])
{
    const char *fname;
    const char *res;

    if (argc < 2)
    {
        printf("Please specify website\n");
        return 1;
    }

    if (argv[2] == NULL)
    {
        fname = "index.html";
        res = "";
    }
    else
    {
        fname = argv[2];
        res = argv[2];
    }

    FILE *f = fopen(fname, "wb");
    if (f == NULL)
    {
        printf("Failed to open %s for write\n", fname);
        return 1;
    }

    printf("Saving to %s...\n", fname);

    if (!WebRequest("GET", argv[1], res, HttpWriteCbk, f))
        printf("Failed to get %s from %s\n", res, argv[1]);

    printf("Wrote %ld bytes to %s.\n", ftell(f), fname);

    fclose(f);
    return 0;
}

int HttpWriteCbk(const void *data, size_t size, void *context)
{
    fwrite(data, size, 1, context);
    fflush(context);
    return 1;
}

int WebRequest(const char *verb, const char *host, const char *resource,
               int (*write_cbk)(const void *, size_t, void *), void *cbk_context)
{
    size_t total_len = 0;
    size_t cur_pos = 0;
    char *header_end = NULL;
    int status = 0;

    //// resolve shit and connect here
    int sck = EstablishConnection(host, "http");
    if (sck == -1)
    {
        printf("Failed to connect to host %s\n", host);
        goto done;
    }

    char buf[8192];
    char line[512];

    //// Send an HTTP request
    snprintf(buf, sizeof(buf),
             "%s /%s HTTP/1.1\r\n"
             "Host: %s\r\n\r\n",
             verb, resource, host);

    if (send(sck, buf, strlen(buf), 0) == -1)
    { // handle other edge cases if desired
        printf("Failed to send HTTP request\n");
        goto done;
    }

    TEXTSCK stream;
    KVPAIR headers[64];
    textsckinit(&stream, sck);

    netgets(line, sizeof(line), &stream);
    printf("%s", line);

    size_t num_headers = 0;

    while (netgets(line, sizeof(line), &stream))
    {
        if (line[0] == '\r' && line[1] == '\n')
            break;

        char *val = strchr(line, ':');
        if (val == NULL)
        {
            printf("Malformed header, skipping...\n");
            continue;
        }

        *val = '\0';
        val = ChompWS(val + 1);

        headers[num_headers].key = strdup(line);
        headers[num_headers].val = strdup(val);
        num_headers++;
    }

    for (int i = 0; i != num_headers; i++)
    {
        printf("Key: %s\t\tval: %s\n", headers[i].key, headers[i].val);
    }

    /*
	//// Read in the whole HTTP header
	while (header_end == NULL && total_len < sizeof(buf)) {
		ssize_t len_received = recv(sck, buf + total_len, sizeof(buf) - total_len, 0);
		if (len_received == -1) {
			printf("Connection forcefully terminated\n");
			goto done;
		} else if (len_received == 0) {
			printf("Received graceful disconnection after only reading %zd bytes of header !??\n", total_len);
			goto done;
		} else {
			cur_pos = total_len;
			total_len += len_received;
		}

		header_end = strnstr(buf + cur_pos, "\r\n\r\n", len_received);
	}

	if (header_end == NULL) {
		printf("HTTP header apparently larger than our buffer...\n");
		goto done;
	}

	*header_end = '\0';

	//// Yay, we found the header, let's parse it
	char *keys[32];
	char *vals[32];

	int num_headers = HttpParseHeader(buf, keys, vals, 32);
	if (num_headers == -1) {
		printf("HTTP header parsing failed\n");
		goto done;
	}
	*/

    long response_body_len = 0;
    int is_chunked_transfer = 0;

    //// Find the header(s) we're interested in
    for (int i = 0; i != num_headers; i++)
    {
        if (!strcmp(headers[i].key, "Content-Length"))
        {
            char *endptr = NULL;
            response_body_len = strtol(headers[i].val, &endptr, 10);

            if (*endptr != '\0')
            {
                // didn't parse cleanly
                printf("Invalid content length \'%s\'\n", headers[i].val);
                goto done;
            }

            is_chunked_transfer = 0;
        }
        else if (!strcmp(headers[i].key, "Transfer-Encoding"))
        {
            if (!strcmp(headers[i].val, "chunked")) // actually, this could be a comma delimited list... *sigh*
                is_chunked_transfer = 1;
        }
    }

    if (response_body_len == 0 && !is_chunked_transfer)
    {
        printf("Failed to find length of response body\n");
        goto done;
    }

    //// Actually receive the body now!
    const char *body_start = header_end + 4;
    size_t body_start_len = total_len - (body_start - buf);
    if (is_chunked_transfer)
    {
        printf("Expecting a chunked response body transfer.\n");
        //HttpRecvBodyChunked(sck, body_start, body_start_len);
    }
    else
    {
        printf("Got length of response body: %ld\n", response_body_len);

        if (!write_cbk(body_start, body_start_len, cbk_context))
            goto done;

        cur_pos = 0;
        total_len = 0;

        while (total_len < response_body_len)
        {
            ssize_t len_received = recv(sck, buf, sizeof(buf), 0);
            if (len_received == -1)
            {
                printf("Connection forcefully terminated.\n");
                goto done;
            }
            else if (len_received == 0)
            {
                printf("Conection closed.\n");
                break;
            }
            else
            {
                cur_pos = total_len;
                total_len += len_received;
            }

            printf("%zu/%zu bytes\n", total_len, response_body_len);
            if (!write_cbk(buf, len_received, cbk_context))
                goto done;
        }
    }

    status = 1;

done:
    close(sck);
    return status;
}

int HttpParseHeader(char *header, char **keys, char **vals, size_t max_headers)
{
    //// Get the status line
    char *sep = strstr(header, "\r\n");
    if (sep == NULL)
        return 1; // no status line?
    *sep = 0;
    printf("Header line: %s\n", header);
    header = sep + 2;

    // Note we can't use strtok or strsep here because we need our delimeter to be multiple chars.
    //for (s = strtok_r(buf, "\r\n", &last); ...etc...) <--- this isn't going to work correctly
    size_t num_headers = 0;

    //// Extract request params
    char *cur = header;
    while (num_headers < max_headers)
    {
        char *sep = strstr(cur, "\r\n");
        if (sep == NULL)
            break;

        *sep = '\0';

        keys[num_headers] = cur; // I believe the key could also possibly have whitespace before the colon...?  TODO check standard

        char *delim = strchr(cur, ':');
        if (delim == NULL)
            return -1;

        *delim++ = '\0';
        delim += strspn(delim, " \v\t\r\n"); // skip whitespace
        vals[num_headers] = delim;

        printf("Got header: %s   ----->   %s\n", keys[num_headers], vals[num_headers]);

        num_headers++;
        cur = sep + 2;
    }

    return num_headers;
}

int EstablishConnection(const char *host, const char *service_str)
{
    struct addrinfo hints;
    struct addrinfo *listp = NULL;
    struct addrinfo *p = NULL;
    int status;
    int sck = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME | AI_ALL | AI_ADDRCONFIG;

    status = getaddrinfo(host, service_str, &hints, &listp);
    if (status != 0)
    {
        printf("getaddrinfo error: (%s)\n", gai_strerror(status));
        goto done;
    }

    for (p = listp; p; p = p->ai_next)
    {
        sck = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sck < 0)
            continue;

        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        if (setsockopt(sck, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            close(sck);
            goto done;
        }

        if (connect(sck, p->ai_addr, p->ai_addrlen) != -1)
            goto done;

        close(sck);
    }
    printf("all connects failed\n");

fail_closefd_freeaddr:
    close(sck);
done:
    if (listp != NULL)
        freeaddrinfo(listp);
    return sck;
}
