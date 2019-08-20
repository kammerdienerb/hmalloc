#include <stdio.h>
#include <stdlib.h>

int main() {
    void *ptr;

    posix_memalign(&ptr, 4096 * 8, 40);
    printf("0x%llx\n", (unsigned long long)ptr);
    free(ptr);
    /* ptr = malloc(4); */
    /* printf("%llu\n", ptr); */
    /* free(ptr); */
    /* ptr = malloc(64); */
    /* printf("%llu\n", ptr); */
    /* free(ptr); */

    /* ptr = valloc(128); */
    /* printf("%llu\n", ptr); */
    /* free(ptr); */
}
