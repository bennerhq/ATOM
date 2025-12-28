// Multiplication-heavy benchmark (new version, C)
#include <stdio.h>
#include <stdlib.h>

int step(int acc, int i, int wrap) {
    acc = acc * 3;
    acc = acc + i;
    if (acc < 0) acc = acc + wrap;
    if (acc > wrap) acc = acc - wrap;
    return acc;
}

int run(int iterations, int wrap) {
    int i = 0;
    int acc = 1;
    while (i < iterations) {
        acc = step(acc, i, wrap);
        i++;
    }
    return acc;
}

int main(int argc, char** argv) {
    int iterations = 100000000;
    int wrap = 1000000;
    if (argc > 1) iterations = atoi(argv[1]);
    if (argc > 2) wrap = atoi(argv[2]);
    int checksum = run(iterations, wrap);
    printf("Iterations: %d\n", iterations);
    printf("Wrap: %d\n", wrap);
    printf("Checksum: %d\n", checksum);
    return 0;
}
