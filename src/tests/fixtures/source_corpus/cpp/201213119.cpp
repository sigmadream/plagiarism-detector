#include <stdio.h>

int main()
{
    int n = 0;

    scanf("%d", &n);
    printf("%d is ", n);
    if (n % 2 == 0)
        puts("Hola");
    else
        puts("Hello");

    return 0;
}
