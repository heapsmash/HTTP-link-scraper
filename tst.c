#include <stdio.h>
#include <stdlib.h>
int main(int argc, char *argv[])
{
    char *endp = NULL;
    long num = strtol(argv[1], &endp, 10);
    printf("Extracted number: %ld, Endp: %s\n", num, endp);
    return 0;
}
