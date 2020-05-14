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

int main(int argc, char *argv[])
{
    char header[8192], *header_tail_ptr;

    int offset = 0;
    while (1)
    {
        ssize_t nread = fake_read(123, header + offset, sizeof(header) - 1);
        if (nread <= 0)
            break;

        offset += nread;
        if ((header_tail_ptr = strstr(header, "\r\n\r\n")) != NULL)
            break;
    }

    *header_tail_ptr = '\0';
    header_tail_ptr += 4;
    header[offset] = '\0';

    char con_len[offset];
    strncpy(con_len, strchr(strstr(header, "Content-Length: "), ' ') + 1, offset - 1);
    *(strchr(con_len, '\r')) = '\0';
    long body_sz = strtol(con_len, NULL, 10);

    char body[body_sz + 1];
    strncpy(body, header_tail_ptr, sizeof body);

    offset = strlen(header_tail_ptr);

    while (1)
    {
        ssize_t nread = fake_read(123, body + offset, sizeof(body) - 1);
        if (nread <= 0)
            break;

        offset += nread;
    }
    body[offset] = '\0';

    puts(header);
    puts(body);

    return 0;
}
