// String concatenation benchmark in C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    int n = 100000;
    if (argc > 1) {
        n = atoi(argv[1]);
    }
    // Allocate enough space for n 'a' characters plus null terminator
    char* s = (char*)malloc(n + 1);
    if (!s) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    for (int i = 0; i < n; ++i) {
        s[i] = 'a';
    }
    s[n] = '\0';
    printf("N: %d\n", n);
    printf("Length: %zu\n", strlen(s));
    free(s);
    return 0;
}
