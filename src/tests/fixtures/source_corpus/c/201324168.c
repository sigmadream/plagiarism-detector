#include <stdio.h>  //puts, scanf, pritf

int main()
{
    int n = 0;

    scanf("%d", &n);
    //printf("%d is ", n);
    if (n % 2 == 0)
        printf("Hi %d", n);
    else
        printf("Hello %d", n);
    //scanf("%d", &n);
   // printf("Hello %d", n);
    //puts("");
    return 0;
}
