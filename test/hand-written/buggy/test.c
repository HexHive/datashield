#include <stdio.h>
#include <time.h>
#include <stdlib.h>

struct foo {
  unsigned x;
  char *y;
  float z;
};

__attribute__((annotate("sensitive"))) struct foo aasdfdsafdsafdsaf;

void print_usage() {
  printf("USAGE: test <a positive integer> <another positive integer>\n");
}

unsigned min_after(struct foo* arr, unsigned n, int start) {
  unsigned winner = start;
  for (unsigned i = start; i < n; ++i) {
    if (arr[i].x < arr[winner].x) {
      winner = i;
    }
  }
  return winner;
}

void insertion_sort(struct foo* arr, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    unsigned swap = min_after(arr, n, i);
    struct foo tmp = arr[i];
    arr[i] = arr[swap];
    arr[swap] = tmp;
  }
}

int main(int argc, char** argv) {
  srand(time(NULL));
  if (argc == 1) {
    print_usage();
    return 0;
  } 
  int n = atoi(argv[1]);
  if (n <= 0) {
    print_usage();
    return 0;
  }
  int m = atoi(argv[2]);
  if (m <= 0) {
    print_usage();
    return 0;
  }
  struct foo* arr = malloc(sizeof(struct foo)*n);
  for (unsigned i = 0; i < n; ++i) {
    arr[i].x = rand();
  }
  insertion_sort(arr, n);
  printf("arr[%d]: %d\n", m, arr[m].x);
}
