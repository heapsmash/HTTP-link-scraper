#include <stdio.h>

int main()
{
    int a = 1, b = 2, c = 3, d = 4;
    int x = a;
    if (a > b)
        if (b < c)
            x = b;
        else
            x = c;
    printf("%d", x); 
    return 0; 
}