// C version of perf_array_sum.atom
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
    int64_t size = 10000000;
    if (argc > 1) size = parse_arg(argv[1], size);
    int64_t* arr = (int64_t*)malloc(size * sizeof(int64_t));
    for (int64_t i = 0; i < size; ++i) arr[i] = i;
    int64_t sum = 0;
    for (int64_t i = 0; i < size; ++i) sum += arr[i];
    printf("Size: %lld\n", (long long)size);
    printf("Checksum: %lld\n", (long long)sum);
    free(arr);
    return 0;
}
