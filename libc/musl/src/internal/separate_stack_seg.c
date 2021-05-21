#include "libc.h"
#include <stdint.h>

static void dummy(void) {}
weak_alias(dummy, __restrict_segments);
static void dummy1(int argc, char ***argvp, char ***envpp) {}
weak_alias(dummy1, __sep_stack_seg_init);

__attribute__((__visibility__("hidden")))
uintptr_t __safestack_addr_hint(size_t size)
{
	return 0;
}
