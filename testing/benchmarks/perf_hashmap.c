// C version of perf_hashmap.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMPTY_KEY INT64_MIN

typedef struct {
    int64_t key;
    int64_t value;
} Entry;

static int64_t parse_arg(const char* arg, int64_t fallback) {
    if (!arg) return fallback;
    char* end = NULL;
    int64_t val = strtoll(arg, &end, 10);
    return (end && *end == '\0') ? val : fallback;
}

int main(int argc, char** argv) {
    int64_t n = 100000;
    if (argc > 1) n = parse_arg(argv[1], n);
    Entry* map = (Entry*)malloc(n * sizeof(Entry));
    for (int64_t i = 0; i < n; ++i) map[i].key = EMPTY_KEY;
    for (int64_t i = 0; i < n; ++i) {
        int64_t k = (i * 17) % n;
        int64_t idx = k % n;
        while (map[idx].key != EMPTY_KEY && map[idx].key != k) idx = (idx + 1) % n;
        map[idx].key = k;
        map[idx].value = i;
    }
    int64_t sum = 0;
    for (int64_t i = 0; i < n; ++i) {
        int64_t k = (i * 17) % n;
        int64_t idx = k % n;
        while (map[idx].key != EMPTY_KEY && map[idx].key != k) idx = (idx + 1) % n;
        if (map[idx].key == k) sum += map[idx].value;
    }
    printf("N: %lld\n", n);
    printf("Result: %lld\n", sum);
    free(map);
    return 0;
}
