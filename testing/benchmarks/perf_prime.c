// C version of perf_prime.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t parse_arg(const char* arg, int64_t fallback) {
    if (!arg) return fallback;
    char* end = NULL;
    int64_t val = strtoll(arg, &end, 10);
    return (end && *end == '\0') ? val : fallback;
}

static int is_prime(int64_t n) {
    if (n <= 1) return 0;
    if (n == 2) return 1;
    if ((n % 2) == 0) return 0;
    for (int64_t i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    int64_t limit = 10000000;
    if (argc > 1) limit = parse_arg(argv[1], limit);
    int64_t count = 0;
    for (int64_t n = 2; n <= limit; ++n) {
        if (is_prime(n)) ++count;
    }
    printf("primes up to %lld: %lld\n", (long long)limit, (long long)count);
    return 0;
}
