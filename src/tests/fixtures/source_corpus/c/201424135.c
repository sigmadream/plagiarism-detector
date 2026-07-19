#include <stdio.h>

int main()
{
    int n = 0;
    scanf("%d", &n); //decimal number , n은 값 , n에 값을 입력하기 위해 주소를 가져온다?
    /*printf("Hello %d",n);
    puts("");*/

    /*printf("%d is ",n);
    if (n % 2==0)
        puts("even.");
    else
        puts("odd.");*/

    if (n % 2 == 0)
        printf("Hi %d\n",n);
    else if(n == 0)
        printf("zero\n");
    else
        printf("Hello %d\n",n);

    return 0;

}
