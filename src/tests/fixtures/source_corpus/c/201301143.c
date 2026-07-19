#include <stdio.h>

int main()
{
    int n = 0;

    scanf("%d", &n);
    if (n % 2 == 0)
        printf("Hi ");
    else
        printf("Hello ");
    printf("%d", n);

    return 0;
}
