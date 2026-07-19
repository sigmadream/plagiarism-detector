#include <stdio.h>

int main()
{
    int n = 0;

    scanf("%d", &n);
    if(n%2==0)
    {
        printf("Hi %d", n);
    }
    else
    {
        printf("Hello %d", n);
    }
    return 0;
    }
