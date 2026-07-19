#include <stdio.h>

int main()
{
    int N = 0;
    scanf(" %d", &N);

    if(N % 2 == 0)
        printf("Hi %d\n", N);
    else
        printf("Hello %d\n", N);

    return 0;
}
