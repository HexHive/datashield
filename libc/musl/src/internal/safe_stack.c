#define _GNU_SOURCE
#include "pthread_impl.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>

#if SAFE_STACK

static bool unsafe_stack_ptr_inited = false;

/* base address of safe stack allocated most recently */
__attribute__((__visibility__("hidden")))
uintptr_t __stack_base;

void *__mmap(void *, size_t, int, int, int, off_t);
int __munmap(void *, size_t);
int __mprotect(void *, size_t, int);
void __restrict_segments(void);

extern int __unsafe_stack_start, __unsafe_stack_end;

__attribute__((__visibility__("hidden")))
void __init_unsafe_stack(void)
{
	size_t stack_size;
	pthread_attr_t attr;
	struct pthread *self;

	if (unsafe_stack_ptr_inited)
		return;

	self = __pthread_self();

	/* Set the unsafe stack pointer in the current TCB to the statically-allocated
	 * unsafe stack, since some of the subroutines invoked below may use the
	 * unsafe stack. */
	self->unsafe_stack_ptr = &__unsafe_stack_end;

	if (__mprotect(&__unsafe_stack_start, 4096, PROT_NONE)
	    && errno != ENOSYS)
		a_crash();

	unsafe_stack_ptr_inited = true;
}

static struct pthread preinit_tcb = {
	.unsafe_stack_ptr = &__unsafe_stack_end
};

/* Install a TCB with just the unsafe stack pointer initialized. */
__attribute__((__visibility__("hidden")))
void __preinit_unsafe_stack(void)
{
	if (unsafe_stack_ptr_inited)
		return;

	__set_thread_area(&preinit_tcb);
}

#define ROUND(x) (((x)+PAGE_SIZE-1)&-PAGE_SIZE)

uintptr_t __safestack_addr_hint(size_t size);

__attribute__((__visibility__("hidden")))
int __safestack_init_thread(struct pthread *restrict new, const pthread_attr_t *restrict attr)
{
	size_t size, guard;
	unsigned char *map = 0;

	new->unsafe_stack_ptr = new->stack;

	guard = ROUND(DEFAULT_GUARD_SIZE + attr->_a_guardsize);
	size = ROUND(new->stack_size + guard);

	uintptr_t try_map = __safestack_addr_hint(size);

	map = __mmap((void *)try_map, size, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (map == MAP_FAILED)
		goto fail;
	if (__mprotect(map+guard, size-guard, PROT_READ|PROT_WRITE)
	    && errno != ENOSYS) {
		__munmap(map, size);
		goto fail;
	}

	new->safe_stack_base = map;
	new->safe_stack_size = size;
	new->stack = map + size;

	__stack_base = (uintptr_t)map;

	__restrict_segments();

	return 0;
fail:
	return EAGAIN;
}

void __safestack_pthread_exit(struct pthread *self)
{
	if (self->detached && self->safe_stack_base)
		__munmap(self->safe_stack_base, self->safe_stack_size);
}

#else /*SAFE_STACK*/

static void dummy(void) {}
weak_alias(dummy, __preinit_unsafe_stack);
weak_alias(dummy, __init_unsafe_stack);
int dummy1(struct pthread *restrict thr, const pthread_attr_t *restrict attr)
{
	return 0;
}
weak_alias(dummy1, __safestack_init_thread);
void dummy2(struct pthread *self) {}
weak_alias(dummy2, __safestack_pthread_exit);

#endif /*SAFE_STACK*/
