#define _GNU_SOURCE
#include "atomic.h"
#include "libc.h"
#include "syscall.h"
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define BOUNDARY (1ull << 28)

//#if SEP_STACK_SEG

extern uintptr_t __stack_base;

typedef uintptr_t uptr;

static void mpx_set_bnd0_ub(uptr ub) {
  __asm__ __volatile__("bndmk %0, %%bnd0" :: "m"(*(char *)ub));
}

__attribute__((__visibility__("hidden")))
void __sep_stack_seg_init(int argc, char ***argvp, char ***envpp)
{
  unsigned cpuid_res;
  __asm__ __volatile__("cpuid" : "=b"(cpuid_res) : "a"(7), "c"(0) : "rdx");
  if (((cpuid_res >> 14) & 1) != 1)
	// No MPX support
	return;
  __asm__ __volatile__("cpuid" : "=a"(cpuid_res) : "a"(0xD), "c"(0) : "rbx", "rdx");
  if (((cpuid_res >> 3) & 3) != 3)
	// Insufficient MPX support
	return;
  __asm__ __volatile__("cpuid" : "=a"(cpuid_res) : "a"(0xD), "c"(1) : "rbx", "rdx");
  if (((cpuid_res >> 1) & 1) != 1)
	// No XSAVEC support
	return;

  struct {
	char pad[512];
	uint64_t xstate_bv;
	uint64_t xcomp_bv;
	char pad1[48];
	struct {
	  uint64_t en : 1;
	  uint64_t bprv : 1;
	  uint64_t : 10;
	  uint64_t tblbase : 52;
	} bndcfgu;
	uint64_t bndstatus;
  } xsave_area __attribute__((aligned(64)));

  memset(&xsave_area, 0, sizeof(xsave_area));
  xsave_area.bndcfgu.en = 1;
  xsave_area.bndcfgu.bprv = 1;
  xsave_area.xcomp_bv = 0x8000000000000010ull;
  xsave_area.xstate_bv = 0x10;
  _xrstor(&xsave_area, 1 << 4);

  mpx_set_bnd0_ub(BOUNDARY);
}

uintptr_t __safestack_addr_hint(size_t size)
{
	/* Try to allocate the new safe stack just below the lowest existing safe
	 * stack to help avoid a data segment limit that is too low and causes
	 * faults when accessing non-stack data above the limit. */

	return __stack_base - size;
}

static void dummy(void) {}
weak_alias(dummy, __restrict_segments);

//#else
#if 0

static void dummy(void) {}
weak_alias(dummy, __restrict_segments);
static void dummy1(int argc, char ***argvp, char ***envpp) {}
weak_alias(dummy1, __sep_stack_seg_init);

__attribute__((__visibility__("hidden")))
uintptr_t __safestack_addr_hint(size_t size)
{
	return 0;
}

#endif /*SEP_STACK_SEG*/
