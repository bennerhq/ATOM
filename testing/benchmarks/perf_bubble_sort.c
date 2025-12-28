// C version of perf_bubble_sort.atom
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
    int64_t n = 20000;
    if (argc > 1) n = parse_arg(argv[1], n);
    int64_t* arr = (int64_t*)malloc(n * sizeof(int64_t));
    for (int64_t i = 0; i < n; ++i) arr[i] = n - i;
    for (int64_t i = 0; i < n; ++i) {
        for (int64_t j = 0; j < n - 1; ++j) {
            if (arr[j] > arr[j+1]) {
                int64_t tmp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = tmp;
            }
        }
    }
    printf("N: %lld\n", n);
    printf("First: %lld\n", arr[0]);
    free(arr);
    return 0;
}
