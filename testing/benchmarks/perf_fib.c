// C version of perf_fib.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t parse_arg(const char* arg, int64_t fallback) {
    if (!arg) return fallback;
    char* end = NULL;
    int64_t val = strtoll(arg, &end, 10);
    return (end && *end == '\0') ? val : fallback;
}

static int64_t fib(int64_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, char** argv) {
    int64_t n = 45;
    if (argc > 1) n = parse_arg(argv[1], n);
    int64_t result = fib(n);
    printf("N: %lld\n", (long long)n);
    printf("Result: %lld\n", (long long)result);
    return 0;
}
