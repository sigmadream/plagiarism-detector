#include <stdio.h>   //puts, scanf, printf (같은 라인에 표시)

int main()
{
    int n=0;

    scanf("%d", &n);  //%d read the decimal number
    if (n %2 == 0 )
        printf("Hi %d", n);
    else
        printf("Hello %d", n);

    return 0;
}
