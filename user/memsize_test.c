#include "kernel/types.h"
#include "user/user.h"

int main() {
    int before, after_alloc, after_free;
    before = memsize();
    printf("Memory size before allocation: %d\n", before);

    char *arr = malloc(20000); // Allocate 20000 bytes

    after_alloc = memsize();
    printf("Memory size after allocation: %d\n", after_alloc);

    free(arr); // Free the allocated memory

    after_free = memsize();
    printf("Memory size after freeing: %d\n", after_free);
    
    exit(0);
}