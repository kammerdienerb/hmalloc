#include <stdio.h>
#include <stdlib.h>

int main() {
    void *ptr1,
         *ptr2,
         *ptr3;
    void *ptr4,
         *ptr5,
         *ptr6;
    void *ptr7,
         *ptr8,
         *ptr9;
    void *ptr10,
         *ptr11,
         *ptr12;

    ptr1 = malloc(7000);
    ptr2 = malloc(14000);
    ptr3 = malloc(21000);
    

    write(0, "free 1\n", 7);
    free(ptr1);

    ptr4 = malloc(70);
    ptr5 = malloc(1400);
    ptr6 = malloc(21000);
    
    write(0, "free 2\n", 7);
    free(ptr2);
    
    ptr7 = malloc(7000);
    ptr8 = malloc(140);
    ptr9 = malloc(21000);

    write(0, "free 3\n", 7);
    free(ptr3);
    
    ptr10 = malloc(700);
    ptr11 = malloc(14);
    ptr12 = malloc(210);
    
    free(ptr8);
    free(ptr9);
    free(ptr7);
    
    free(ptr4);
    free(ptr6);
    free(ptr5);

    free(ptr12);
    free(ptr10);
    free(ptr11);
}
