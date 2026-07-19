#include <stdio.h>

int main() {
    int N = 0;
    scanf("%d", &N);
    if (N%2 == 0) {
        printf("Hola %d", N);
    }
    else {
        printf("Hello %d", N);
    }
    return 0;
}
