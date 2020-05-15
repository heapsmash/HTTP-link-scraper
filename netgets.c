#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

char g_buf[BUFSIZ];

int main(int argc, char *argv[])
{
    int fd = open("html.dat", O_RDONLY);

    if (fd < 0)
        fprintf(stderr, "open error");

    return 0;
}

char *netgets(char *s, size_t size, int fd)
{
    char c;

    ssize_t nread = read(fd, g_buf, BUFSIZ - 1);

    if (nread < 0)
    {
        fprintf(stderr, "read error");
        return NULL;
    }

    size_t i;
    for (i = 0; i < size - 1 && (c = g_buf[i]) != '\n'; i++)
        s[i] = c;

    if (c == '\n')
    {
        s[i] = c;
        ++i;
    }
    s[i] = '\0';

    return s;
}
