// C version of perf_calls.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t parse_arg(const char* arg, int64_t fallback) {
    if (!arg) return fallback;
    char* end = NULL;
    int64_t val = strtoll(arg, &end, 10);
    return (end && *end == '\0') ? val : fallback;
}

static int64_t tick(int64_t acc, int64_t i, int64_t wrap) {
    acc += i;
    if (acc > wrap) acc -= wrap;
    return acc;
}

int main(int argc, char** argv) {
    int64_t iterations = 100000000, wrap = 1000000;
    if (argc > 1) iterations = parse_arg(argv[1], iterations);
    if (argc > 2) wrap = parse_arg(argv[2], wrap);
    int64_t acc = 0;
    for (int64_t i = 0; i < iterations; ++i) {
        acc = tick(acc, i, wrap);
    }
    printf("Iterations: %lld\n", (long long)iterations);
    printf("Wrap: %lld\n", (long long)wrap);
    printf("Checksum: %lld\n", (long long)acc);
    return 0;
}
