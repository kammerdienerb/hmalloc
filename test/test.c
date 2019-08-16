#include <stdio.h>
#include <stdlib.h>

int main() {
    void *ptr;

    ptr = malloc(1234);
    printf("%p\n", ptr);
    free(ptr);
    ptr = malloc(4);
    printf("%p\n", ptr);
    free(ptr);
    ptr = malloc(64);
    printf("%p\n", ptr);
    free(ptr);

    ptr = valloc(128);
    printf("%p\n", ptr);
    free(ptr);
}
