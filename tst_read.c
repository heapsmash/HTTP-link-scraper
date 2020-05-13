#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

ssize_t fake_read(int fd, void *buf, size_t nbytes)
{
    static size_t curpos = 0;
    const char testdata[] =
        "HTTP/1.1 200 OK\r\n"
        "Date: Mon, 23 May 2005 22:38:34 GMT\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: 138\r\n"
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

int main(int argc, char *argv[])
{
    char buf[8192];

    int offset = 0;
    while (1) /* read until header is recieved */
    {
        ssize_t nreceived = fake_read(123, buf + offset, sizeof(buf) - 1);
        if (nreceived <= 0)
            break;

        offset += nreceived;

        if (strstr(buf, "\r\n\r\n") != NULL)
            break;
    }
    buf[offset] = '\0';

    char *tmp_body = strstr(buf, "\r\n\r\n") + 4; /* store over read body */
    *(strstr(buf, "\r\n\r\n")) = '\0';            /* truncate header */

    char header[sizeof(buf) + 1]; /* store header */
    strncpy(header, buf, sizeof(buf));

    char *tmp = strstr(buf, "Content-Length: "); /* get content length */
    *(strchr(tmp, '\r')) = '\0';
    long content_len = strtol((strchr(tmp, ' ') + 1), NULL, 10);

    char body[content_len];
    strncpy(body, tmp_body, sizeof body);

    offset = strlen(tmp_body);
    while (1) /* read until body is recieved */
    {
        ssize_t nreceived = fake_read(123, body + offset, sizeof(body) - 1);
        if (nreceived <= 0)
            break;

        offset += nreceived;
    }

    printf("%d\n", offset);

    puts("--- HEADER ---");
    printf("%s\n", header);
    puts("--- BODY ---");
    printf("%s", body);

    return 0;
}
