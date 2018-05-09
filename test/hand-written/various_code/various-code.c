/* This file contains various code constructs to illustrate some
   of the cases that the DataShield pass needs to handle. */

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

typedef uint64_t sens_arr[10];

__attribute__((annotate("sensitive")))
sens_arr dummy;

volatile int ns_x[5];

int non_fp_func(int x, int y) {
  return x + y;
}

volatile int memcpy_dest;

int qsort_cmp(const void *a, const void *b) {
  return 0;
}

int qsort_cmp1(const long *a, const long *b) {
  return 1;
}

typedef int (*cmpfun)(const void *, const void *);

int fp_func(int* x, double y) {
  memcpy(&memcpy_dest, x, sizeof(*x));
  qsort(x, 1, sizeof(*x), qsort_cmp);
  qsort(x, 1, sizeof(*x), (cmpfun)qsort_cmp1);
  return *x + (int)y;
}

int fp_func2(int* x, double y) {
  return *x + (int)y;
}

struct sens_fp_call_inf_t {
  volatile int (*to_call)(int*, double);
};
__attribute__((annotate("sensitive"))) struct sens_fp_call_inf_t dummySf;
volatile struct sens_fp_call_inf_t sens_fp_call_inf;

volatile struct {
  volatile int (*to_call)(int*, double);
  volatile int (*to_call_1)(int*, double);
} fp_call_inf = { fp_func, fp_func };

__attribute__((noinline))
int ind_invoke(int (*to_inv)(int*, double), int *x, double y) {
  sens_fp_call_inf.to_call = to_inv;
  int (*to_inv_tmp)(int*, double) = sens_fp_call_inf.to_call;
  return to_inv_tmp(x, y);
}

#define USE_FP_TO_CALL

#ifdef USE_FP_TO_CALL
volatile int volatile (*fp_to_call)(int*, double) = fp_func;
#endif

volatile int     sens_fp = 7;
volatile int     sens_fp2 = 9;

__attribute__((noinline))
volatile int *volatile fp_ptr() {
  return (volatile int *volatile)(sens_fp? &sens_fp : &sens_fp2);
}

//array of structs and array of pointers to struct
struct foo {
  char y;
  double z;
  unsigned x;
};

__attribute__((annotate("sensitive"))) struct foo dummyFoo;

struct foo fooArr1[2];
struct foo foo2;
struct foo fooArr3[3];

__attribute__((annotate("sensitive")))
struct foo *fooPtrArr[] = {
  &fooArr1[0],
  &fooArr1[1],
  &foo2,
  &fooArr3[0],
  &fooArr3[1],
  &fooArr3[2]
};

//variadic function
void myVarStrTest2(int num, va_list args) {
  char *a;
  for (int i = 0; i < num; i++) {
    a = va_arg(args, char*);
    if (a != NULL)
      printf("arg %d is %s.\n", i + 1, a);
  }
}

void myVarStrDerefTest2(int num, va_list args) {
  char *a;
  for (int i = 0; i < num; i++) {
    a = va_arg(args, char*);
    if (a != NULL)
      printf("arg %d is %s. First char is %c.\n", i + 1, a, a[0]);
  }
}

void myVarFooTest2(int num, va_list args) {
  struct foo *a;
  printf("address of a is %p\n", &a);
  for (int i = 0; i < num; i++) {
    a = va_arg(args, struct foo*);
    if (a != NULL) {
      printf("address in a is %p\n", a);
    }
  }
}

void myVarStrTest(int num, ...) {
  va_list args;
  va_start(args, num);
  myVarStrTest2(num, args);
  va_end(args);
}

void myVarStrDerefTest(int num, ...) {
  va_list args;
  va_start(args, num);
  myVarStrDerefTest2(num, args);
  va_end(args);
}

void myVarFooTest(int num, ...) {
  va_list args;
  va_start(args, num);
  myVarFooTest2(num, args);
  va_end(args);
}

int main(int argc, const char *argv[]) {
  char *home_envp = getenv("HOME");
  if (*home_envp != NULL) {
    return 1;
  }
  uint64_t x[10] = { 0x8, 0x75, 0x93, 0x822, 0x46, 0x90, 0x7243, 0x8239, 0x8452, 0x83902 };
#ifdef SUPPORTS_YOLK
  __attribute__((yolk))
#endif
    uint64_t y[10] = { 0x18, 0x175, 0x193, 0x1822, 0x146, 0x190, 0x17243, 0x18239, 0x18452, 0x183902 };

  uint64_t intermediate[10];
  uint64_t sensitive_intermediate[10];
  for (int i = 0; i < 200; i++) {
    intermediate[0] ^= x[3];
    intermediate[1] ^= x[4];
    intermediate[2] ^= x[5];
    intermediate[3] ^= x[6];
    intermediate[4] ^= x[7];
    intermediate[5] ^= x[8];
    intermediate[6] ^= x[9];
    intermediate[7] ^= x[0];
    intermediate[8] ^= x[1];
    intermediate[9] ^= x[2];
    sensitive_intermediate[0] ^= x[3] ^ y[6];
    sensitive_intermediate[1] ^= x[4] ^ y[7];
    sensitive_intermediate[2] ^= x[5] ^ y[8];
    sensitive_intermediate[3] ^= x[6] ^ y[9];
    sensitive_intermediate[4] ^= x[7] ^ y[0];
    sensitive_intermediate[5] ^= x[8] ^ y[1];
    sensitive_intermediate[6] ^= x[9] ^ y[2];
    sensitive_intermediate[7] ^= x[0] ^ y[3];
    sensitive_intermediate[8] ^= x[1] ^ y[4];
    sensitive_intermediate[9] ^= x[2] ^ y[5];

    for (int j = 0; j < 10; j++) {
      intermediate[j] <<= 2;
      sensitive_intermediate[j] <<= 2;
    }
  }

  uint64_t res = (int)(intermediate[0] ^
    intermediate[1] ^
    intermediate[2] ^
    intermediate[3] ^
    intermediate[4] ^
    intermediate[5] ^
    intermediate[6] ^
    intermediate[7] ^
    intermediate[8] ^
    intermediate[9] ^
    sensitive_intermediate[0] ^
    sensitive_intermediate[1] ^
    sensitive_intermediate[2] ^
    sensitive_intermediate[3] ^
    sensitive_intermediate[4] ^
    sensitive_intermediate[5] ^
    sensitive_intermediate[6] ^
    sensitive_intermediate[7] ^
    sensitive_intermediate[8] ^
	       sensitive_intermediate[9]);

  puts("Done computing result: ");
  printf("%llu\n", res);

  // Test that sensitivity of a return value from a libc routine
  // that accepts the allocated return value as a parameter
  // propagates to that input allocation.  If this fails, then
  // the attempt to dereference the result from strchr below will
  // fail, since *strchr_res will be treated as sensitive, but
  // it will point to the non-sensitive region.  However, it is
  // possible that the compiler will statically elide the bounds
  // check, which was observed during testing, in which case
  // the lack of sensitivity propagation can be detected by
  // examining the IR.
  char strchr_in[10] = { 'h', 'i', '\0' };
  char *strchr_res = strchr(strchr_in, 'i');
  // Combine result with sensitive_intermediate to cause
  // result to be marked as sensitive.
  sensitive_intermediate[0] += *strchr_res;

  // Test that gmtime is replaced with safeGmtime when its result
  // is marked as sensitive.
  struct tm *gmtime_res = gmtime(time(NULL));
  // Combine tm_sec with sensitive_intermediate to cause
  // *gmtime_res to be marked as sensitive.
  sensitive_intermediate[0] += gmtime_res->tm_sec;

  ns_x[ns_x[0]] += 7;

#ifdef USE_FP_TO_CALL
  if (ns_x[2])
    fp_to_call = fp_func2;
#endif
  
// test array related bounds
  fooPtrArr[0]->x = 0;
  fooPtrArr[1]->x = 1;
  fooPtrArr[2]->x = 2;
  fooPtrArr[3]->x = 3;
  fooPtrArr[4]->x = 4;
  fooPtrArr[5]->x = 5;

  // test variadic function
  //ds bounds violation
  myVarFooTest(1, fooPtrArr[0], fooPtrArr[1], fooPtrArr[2], &fooArr1[0], &fooArr1[1], &foo2);
  //mpx bounds violation
  myVarStrTest(3, "another test", "string", "only");
  myVarStrDerefTest(3, "another test", "string", "only");

  typedef int (*phi_to_call_t)(char*, double);
  typedef int (*phi_to_call_i_t)(int*, double);

  volatile int accum = 1;
  
  int (*phi_to_call)(int*, double);
  phi_to_call_t phi_to_call_casted;
  int phi_func_res = 0;
  switch (accum) {
  case 0:
    phi_to_call = fp_func;
    phi_to_call_casted = (phi_to_call_t)fp_func;
    break;
  case 1:
    phi_to_call = fp_func2;
    phi_to_call_casted = (phi_to_call_t)fp_func2;
    break;
  default:
    phi_to_call = fp_func2;
    phi_to_call_casted = (phi_to_call_t)fp_func;
    break;
  }
  phi_func_res |= phi_to_call(ns_x+2, 1.0);
  phi_func_res |= phi_to_call_casted(ns_x+2, 1.0);

  int (*sel_to_call)(int*, double);
  phi_to_call_t sel_to_call_casted;
  int sel_func_res = 0;
  if (accum) {
    sel_to_call = fp_func;
    sel_to_call_casted = (phi_to_call_t)fp_func;
  } else {
    sel_to_call = fp_func2;
    sel_to_call_casted = (phi_to_call_t)fp_func2;
  }
  sel_func_res |= sel_to_call(ns_x+2, 1.0);
  sel_func_res |= sel_to_call_casted(ns_x+2, 1.0);

  phi_to_call_i_t to_call_arith = (phi_to_call_i_t)(((uintptr_t)fp_func) & ~(1ull << 63));

  return (accum?1:0) | fp_call_inf.to_call(ns_x+2, 0.0)
#ifdef USE_FP_TO_CALL
    | fp_to_call(ns_x+2, 0.0)
#if 0
    // This is a regression test for properly setting the (0) bounds for a NULL pointer
    // passed as a sensitive argument.  However, it causes a crash, so it is disabled
    // by default.
    | fp_to_call(NULL, 0.0)
#endif
#endif
#if 0
    // Regression test that causes crash.
    | ind_invoke(fp_func, NULL/*ns_x+2*/, 0.0)
#endif
    | ind_invoke(fp_func, ns_x+2, 0.0)
    | ind_invoke(fp_func2, ns_x+2, 0.0)
    | *fp_ptr()
    | sel_func_res
    | phi_func_res
    | ind_invoke(to_call_arith, ns_x+2, 1.0)
    ;

}
