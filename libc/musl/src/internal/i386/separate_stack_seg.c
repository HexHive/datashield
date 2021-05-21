#define _GNU_SOURCE
#include "atomic.h"
#include "libc.h"
#include "syscall.h"
#include <malloc.h>
#include <stdint.h>
#include <string.h>

#if SEP_STACK_SEG

#include <ldt.h>

char *__strdup(const char *s);

extern uintptr_t __stack_base;

static int modify_ldt(int func, void *ptr, unsigned long bytecount) {
	return syscall(SYS_modify_ldt, func, ptr, bytecount);
}

static void update_ldt(size_t len) {
	struct user_desc stack_desc;
	stack_desc.entry_number = 0;
	stack_desc.base_addr = 0;
	stack_desc.contents = 0; /* data */
	stack_desc.limit = (int)((len - 1) >> 12);
	stack_desc.limit_in_pages = 1;
	stack_desc.read_exec_only = 0;
	stack_desc.seg_not_present = 0;
	stack_desc.seg_32bit = 1;
	stack_desc.useable = 1;

	if (modify_ldt(1, &stack_desc, sizeof(stack_desc)) == -1)
		a_crash();
}

static void verify_ldt_empty(void) {
	uint64_t ldt;

	/* read the current LDT */
	int ldt_len = modify_ldt(0, &ldt, sizeof(ldt));
	if (ldt_len != 0)
		/* LDT not empty */
		a_crash();
}

#define SEG_SEL_LDT 4
#define SEG_SEL_CPL3 3
/* require restricted segment to occupy the first LDT entry */
#define SEG_SEL_RESTRICTED (SEG_SEL_LDT | SEG_SEL_CPL3)

__attribute__((__visibility__("hidden")))
void __restrict_segments(void)
{
	uintptr_t limit, stack_base = __stack_base;
	int data_seg_sel;

	__asm__ __volatile__ ("mov %%ds, %0" : "=r"(data_seg_sel));
	/* assume that ES is identical to DS */

	if ((data_seg_sel & SEG_SEL_LDT) == SEG_SEL_LDT) {
		if (data_seg_sel != SEG_SEL_RESTRICTED)
			a_crash();

		/* Read the current limit from the segment register rather than
		 * relying on __stack_base, since __stack_base is in the
		 * default data segment and could potentially be subject to
		 * memory corruption. */
		__asm__ __volatile__ ("lsl %1, %0" : "=r"(limit) : "m"(data_seg_sel));

		if (limit < stack_base)
			return;
	} else
		verify_ldt_empty();

	update_ldt(stack_base);

	/* Reload the DS and ES segment registers from the new or updated LDT
	 * entry. */
	__asm__ __volatile__ (
	  "mov %0, %%ds\n\t"
	  "mov %0, %%es\n\t"
	  ::
	  "r"(SEG_SEL_RESTRICTED)
	);
}

extern char **__environ;

/* Programs and much of the libc code expect to be able to access the arguments,
 * environment, and auxv in DS, but they are initially located on the stack.
 * This function moves them to the heap. This uses __strdup to copy data from
 * the stack, so it must run before segment limits are restricted.
 */
__attribute__((__visibility__("hidden")))
void __sep_stack_seg_init(int argc, char ***argvp, char ***envpp)
{
	char **argv = *argvp;
	char **envp = *envpp;
	char **environ_end = envp;
	size_t *auxv, *auxv_end;
	char **new_argv = 0;

	while (*environ_end) environ_end++;

	auxv_end = (size_t *)environ_end + 1;
	while (*auxv_end) auxv_end++;
	auxv_end++;

	new_argv = malloc((uintptr_t)auxv_end - (uintptr_t)argvp);
	if (!new_argv)
		a_crash();

	*new_argv = (char *)argc;
	new_argv++;

	*argvp = new_argv;

	for (int i = 0; i < argc; i++)
		new_argv[i] = __strdup(argv[i]);
	new_argv += argc;
	*new_argv = NULL;
	new_argv++;

	*envpp = __environ = new_argv;
	while (envp != environ_end) {
		*new_argv = __strdup(*envp);
		envp++;
		new_argv++;
	}
	*new_argv = NULL;
	envp++;
	new_argv++;

	libc.auxv = (size_t *)new_argv;
	memcpy(new_argv, envp, (uintptr_t)auxv_end - (uintptr_t)envp);

	__restrict_segments();
}

uintptr_t __safestack_addr_hint(size_t size)
{
	/* Try to allocate the new safe stack just below the lowest existing safe
	 * stack to help avoid a data segment limit that is too low and causes
	 * faults when accessing non-stack data above the limit. */

	return __stack_base - size;
}

#else

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
