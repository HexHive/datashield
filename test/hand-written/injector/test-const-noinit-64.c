#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

__attribute__((annotate("sensitive")))
int dummy[10];

int main(int argc, const char *argv[]) {
 
 __attribute__((annotate("yolk")))
 const long y[10];

  long x[10];
  for (int j = 0; j < 10; j++)
    x[j] = y[j];

  int index;
  printf("enter an index from 0 to 6: ");
  fflush(stdout);
  scanf("%d", &index);
  index += 3;
  assert(index < 10);
  printf("index is %d\n", index);
  printf("array element is %lx\n", x[index]);

  printf("first secret of y is : %lx\n", y[0]);

  __attribute__((annotate("yolk")))
  const int value;
  printf("value is %d\n", value);
}

