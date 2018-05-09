#define __SYSCALL_LL_E(x) \
((union { long long ll; long l[2]; }){ .ll = x }).l[0], \
((union { long long ll; long l[2]; }){ .ll = x }).l[1]
#define __SYSCALL_LL_O(x) __SYSCALL_LL_E((x))

static inline long __syscall0(long n)
{
	unsigned long __ret;
	__asm__ __volatile__ (".hidden __vsyscall ; call __vsyscall" : "=a"(__ret) : "a"(n) : "memory");
	return __ret;
}

static inline long __syscall1(long n, long a1)
{
	unsigned long __ret;
	__asm__ __volatile__ (".hidden __vsyscall ; call __vsyscall" : "=a"(__ret) : "a"(n), "d"(a1) : "memory");
	return __ret;
}

static inline long __syscall2(long n, long a1, long a2)
{
	unsigned long __ret;
	__asm__ __volatile__ (".hidden __vsyscall ; call __vsyscall" : "=a"(__ret) : "a"(n), "d"(a1), "c"(a2) : "memory");
	return __ret;
}

static inline long __syscall3(long n, long a1, long a2, long a3)
{
	unsigned long __ret;
	__asm__ __volatile__ (".hidden __vsyscall ; call __vsyscall" : "=a"(__ret) : "a"(n), "d"(a1), "c"(a2), "D"(a3) : "memory");
	return __ret;
}

static inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
	unsigned long __ret;
	__asm__ __volatile__ (".hidden __vsyscall ; call __vsyscall" : "=a"(__ret) : "a"(n), "d"(a1), "c"(a2), "D"(a3), "S"(a4) : "memory");
	return __ret;
}

static inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
	unsigned long __ret;
	__asm__ __volatile__ ("push %6 ; .hidden __vsyscall ; call __vsyscall ; add $4,%%esp" : "=a"(__ret) : "a"(n), "d"(a1), "c"(a2), "D"(a3), "S"(a4), "g"(a5) : "memory");
	return __ret;
}

static inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	unsigned long __ret;
	__asm__ __volatile__ ("push %6 ; .hidden __vsyscall6 ; call __vsyscall6 ; add $4,%%esp" : "=a"(__ret) : "a"(n), "d"(a1), "c"(a2), "D"(a3), "S"(a4), "g"(0+(long[]){a5, a6}) : "memory");
	return __ret;
}

#if !SEP_STACK_SEG
/* The vDSO may not be compiled with support for a separate stack segment.
 * Avoid invoking the vDSO when this feature is enabled, since it may try to
 * access the stack using memory operands with base registers other than EBP or
 * ESP without also using a stack segment override prefix.  A special compiler
 * pass needs to be used to add such prefixes, and it is unlikely that a pass
 * of that sort was applied when the vDSO was compiled.
 */
#define VDSO_USEFUL
#define VDSO_CGT_SYM "__vdso_clock_gettime"
#define VDSO_CGT_VER "LINUX_2.6"
#endif

#define SYSCALL_USE_SOCKETCALL
