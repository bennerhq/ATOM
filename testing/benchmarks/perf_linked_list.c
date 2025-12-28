// C version of perf_linked_list.atom
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    int64_t value;
    struct Node* next;
} Node;

static int64_t parse_arg(const char* arg, int64_t fallback) {
    if (!arg) return fallback;
    char* end = NULL;
    int64_t val = strtoll(arg, &end, 10);
    return (end && *end == '\0') ? val : fallback;
}

static Node* create_list(int64_t n) {
    Node* head = NULL;
    for (int64_t i = n - 1; i >= 0; --i) {
        Node* node = (Node*)malloc(sizeof(Node));
        node->value = i;
        node->next = head;
        head = node;
    }
    return head;
}

static int64_t sum_list(Node* head) {
    int64_t sum = 0;
    Node* curr = head;
    while (curr) {
        sum += curr->value;
        curr = curr->next;
    }
    return sum;
}

static void free_list(Node* head) {
    while (head) {
        Node* next = head->next;
        free(head);
        head = next;
    }
}

int main(int argc, char** argv) {
    int64_t n = 1000;
    if (argc > 1) n = parse_arg(argv[1], n);
    Node* head = create_list(n);
    int64_t sum = sum_list(head);
    free_list(head);
    printf("N: %lld\n", (long long)n);
    printf("Sum: %lld\n", (long long)sum);
    return 0;
}
