#include <stdio.h>

int main()
{
    int n = 0;

    scanf("%d", &n);
    printf("%d is ", n);
    if (n % 2 == 0)
        puts("even.");
    else
        puts("odd.");

    return 0;
}
