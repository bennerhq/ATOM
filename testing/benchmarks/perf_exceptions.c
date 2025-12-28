// C version of perf_exceptions.atom
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
    int64_t n = 1000000;
    if (argc > 1) n = parse_arg(argv[1], n);
    int64_t sum = 0;
    for (int64_t i = 0; i < n; ++i) {
        if (i % 1000 == 0) {
            sum += 1; // Simulate catch
        } else {
            sum += 2;
        }
    }
    printf("N: %lld\n", (long long)n);
    printf("Result: %lld\n", (long long)sum);
    return 0;
}
