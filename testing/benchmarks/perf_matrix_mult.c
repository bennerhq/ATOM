// C version of perf_matrix_mult.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t parse_arg(const char* arg, int64_t fallback) {
    if (!arg) return fallback;
    char* end = NULL;
    int64_t val = strtoll(arg, &end, 10);
    return (end && *end == '\0') ? val : fallback;
}

int main(int argc, char** argv) {
    int64_t n = 100;
    if (argc > 1) n = parse_arg(argv[1], n);
    int64_t** a = (int64_t**)malloc(n * sizeof(int64_t*));
    int64_t** b = (int64_t**)malloc(n * sizeof(int64_t*));
    int64_t** c = (int64_t**)malloc(n * sizeof(int64_t*));
    for (int64_t i = 0; i < n; ++i) {
        a[i] = (int64_t*)malloc(n * sizeof(int64_t));
        b[i] = (int64_t*)malloc(n * sizeof(int64_t));
        c[i] = (int64_t*)calloc(n, sizeof(int64_t));
        for (int64_t j = 0; j < n; ++j) {
            a[i][j] = i + j;
            b[i][j] = i - j;
        }
    }
    for (int64_t i = 0; i < n; ++i)
        for (int64_t j = 0; j < n; ++j)
            for (int64_t k = 0; k < n; ++k)
                c[i][j] += a[i][k] * b[k][j];
    printf("N: %lld\n", n);
    printf("Result: %lld\n", c[0][0]);
    for (int64_t i = 0; i < n; ++i) {
        free(a[i]);
        free(b[i]);
        free(c[i]);
    }
    free(a); free(b); free(c);
    return 0;
}
