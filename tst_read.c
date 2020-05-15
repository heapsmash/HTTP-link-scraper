#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

int GetHTTPContent(int sck, int (*call_back)(const void *, size_t, void *), void *cbk_context);
ssize_t fake_read(int fd, void *buf, size_t nbytes);
int http_body_read_cbk(const void *data, size_t len, void *context);

int main(int argc, char *argv[])
{
    FILE *f = fopen("file.html", "w");

    if (f == NULL) // Handle
        return 1;

    if (GetHTTPContent(123, &http_body_read_cbk, (void *)f) == 0)
        ; // Handle
    fclose(f);

    return 0;
}

ssize_t fake_read(int fd, void *buf, size_t nbytes)
{
    static size_t curpos = 0;
    const char testdata[] =
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 23 May 2005 22:38:34 GMT\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: 157\r\n"
        "Last-Modified: Wed, 08 Jan 2003 23:11:55 GMT\r\n"
        "Server: Apache/1.3.3.7 (Unix) (Red-Hat/Linux)\r\n"
        "ETag: \"3f80f-1b6-3e1cb03b\"\r\n"
        "Accept-Ranges: bytes\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html>\r\n"
        "  <head>\r\n"
        "    <title>An Example Page</title>\r\n"
        "  </head>\r\n"
        "  <body>\r\n"
        "    <p>Hello World, this is a very simple HTML document.</p>\r\n"
        "  </body>\r\n"
        "</html>\r\n";

    static size_t packet_remaining = 0;

    // pretend the http response is passing thru a really shitty router
    if (packet_remaining == 0)
    {
        sleep(0);
        packet_remaining = 50;
    }

    size_t readlen = MIN(sizeof(testdata) - 1 - curpos, nbytes);
    readlen = MIN(readlen, packet_remaining);

    memcpy(buf, testdata + curpos, readlen);
    curpos += readlen;
    packet_remaining -= readlen;

    return readlen;
}

int GetHTTPContent(int sck, int (*call_back)(const void *, size_t, void *), void *cbk_context)
{
    char header[8192], body[8192], *body_start;
    int offset = 0;
    size_t body_head_len;

    ssize_t nread;
    while (1)
    {
        nread = fake_read(sck, header + offset, sizeof(header) - 1);
        if (nread <= 0)
            break;

        offset += nread;
        body_start = strstr(header, "\r\n\r\n");

        if (body_start != NULL)
        {
            *body_start = '\0';

            // Skip past the header delimiter
            body_start += 4;
            break;
        }
    }

    if (nread < 0)
        return 0;

    body_head_len = offset - (body_start - header);

    printf("%ld\n", body_head_len);

    char *head_ptr = strstr(header, "Content-Length: ");
    if (head_ptr == NULL)
    {
        fprintf(stderr, "Content-Length not found in header\n");
        return 0;
    }

    // 15 is the number of bytes in "Content-Length"
    long content_len = strtol(head_ptr + 15, NULL, 10);
    call_back(body_start, body_head_len, cbk_context);

    content_len -= body_head_len;
    while (1)
    {
        nread = fake_read(sck, body, sizeof(body) - 1);

        if (nread < 0)
            break;

        if (call_back(body, nread, cbk_context) == 0)
            break;

        content_len -= nread;

        if (content_len == 0)
            break;
    }

    if (nread < 0)
        return 0;

    return 1;
}

int http_body_read_cbk(const void *data, size_t len, void *context)
{
    return fwrite(data, 1, len, (FILE *)context) == len;
}
