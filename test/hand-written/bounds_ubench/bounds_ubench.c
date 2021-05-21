#include <stdint.h>
#include <stdio.h>
#include <x86intrin.h>

#define YOLK __attribute__((annotate("yolk")))

#define NUM_ROUNDS 1000000000ll

#define SENS_VAL_SZ 4

typedef int64_t sens_val[SENS_VAL_SZ];

__attribute__((annotate("sensitive")))
sens_val dummy;

volatile int64_t y;

uint64_t rdtsc() {
  _mm_mfence();
  uint64_t retval = _rdtsc();
  _mm_mfence();

  return retval;
}

#define X_INIT { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }

__attribute__((noinline))
void do_arr_round_yolk(int i) {
  const YOLK sens_val x = X_INIT;

  y += x[i];
}

__attribute__((noinline))
void do_round_yolk() {
  const YOLK int64_t x = 5;

  y += x;
}

__attribute__((noinline))
void do_arr_round(int i) {
  const sens_val x = X_INIT;

  y += x[i];
}

__attribute__((noinline))
void do_round() {
  const int64_t x = 7;

  y += x;
}

void spin_for_a_second() {
  uint64_t start_tm = rdtsc();
  while ((start_tm + (1ull << 32)) < rdtsc());
}

__attribute__((noinline))
uint64_t test_arr_yolk() {
  y = 0;
  do_arr_round_yolk(0);
  uint64_t start_tm_arr_yolk = rdtsc();
  for (int64_t i = 0; i < NUM_ROUNDS; i++) {
    do_arr_round_yolk(y);
    y = i % SENS_VAL_SZ;
  }
  uint64_t end_tm_arr_yolk = rdtsc();

  return end_tm_arr_yolk - start_tm_arr_yolk;
}

__attribute__((noinline))
uint64_t test_yolk() {
  do_round_yolk();
  uint64_t start_tm_yolk = rdtsc();
  for (int64_t i = 0; i < NUM_ROUNDS*4; i++) {
    do_round_yolk();
  }
  uint64_t end_tm_yolk = rdtsc();

  return end_tm_yolk - start_tm_yolk;
}

__attribute__((noinline))
uint64_t test_arr() {
  y = 0;
  do_arr_round(0);
  uint64_t start_tm_arr = rdtsc();
  for (int64_t i = 0; i < NUM_ROUNDS; i++) {
    do_arr_round(y);
    y = i % SENS_VAL_SZ;
  }
  uint64_t end_tm_arr = rdtsc();

  return end_tm_arr - start_tm_arr;
}

__attribute__((noinline))
uint64_t test() {
  do_round();
  uint64_t start_tm = rdtsc();
  for (int64_t i = 0; i < NUM_ROUNDS*4; i++) {
    do_round();
  }
  uint64_t end_tm = rdtsc();

  return end_tm - start_tm;
}

int main(int argc, const char *argv[]) {
  // prod the clock frequency up:
  spin_for_a_second();

  uint64_t arr_yolk_tm = test_arr_yolk();
#ifdef TEST_SCALAR
  uint64_t yolk_tm = test_yolk();
#endif
  uint64_t arr_tm = test_arr();
#ifdef TEST_SCALAR
  uint64_t tm = test();
#endif
  
  printf("yolk array duration (cycles): %lu\n", arr_yolk_tm);
#ifdef TEST_SCALAR
  printf("yolk scalar duration (cycles): %lu\n", yolk_tm);
#endif
  printf("non-yolk array duration (cycles): %lu\n", arr_tm);
#ifdef TEST_SCALAR
  printf("non-yolk scalar duration (cycles): %lu\n", tm);
#endif
  
  return 0;
}
