// C version of perf_branch.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t parse_arg(const char* text, int64_t fallback) {
    if (!text) return fallback;
    char* end = NULL;
    int64_t value = strtoll(text, &end, 10);
    return (end && *end == '\0') ? value : fallback;
}

int main(int argc, char** argv) {
    int64_t iterations = 100000000, wrap = 1000000;
    if (argc > 1) iterations = parse_arg(argv[1], iterations);
    if (argc > 2) wrap = parse_arg(argv[2], wrap);
    int64_t acc = 0;
    for (int64_t i = 0; i < iterations; ++i) {
        if ((i % 2) == 0) {
            acc += i;
        } else {
            acc -= i;
        }
        if (acc < 0) acc += wrap;
        if (acc > wrap) acc -= wrap;
    }
    printf("Iterations: %lld\n", (long long)iterations);
    printf("Wrap: %lld\n", (long long)wrap);
    printf("Checksum: %lld\n", (long long)acc);
    return 0;
}
