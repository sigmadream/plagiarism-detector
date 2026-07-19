#include <stdio.h>

int main()
{
	int n = 0;

	scanf("%d", &n);
	printf("%d is ", n);
	if (n % 2 == 0)
		puts("Hi &n");
	else
		puts("Hello &n");

	return 0;
}