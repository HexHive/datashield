#define _GNU_SOURCE
#include <elf.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>
#include <sched.h>
#include <string.h>
#include "syscall.h"
#include "atomic.h"
#include "libc.h"
#include "datashield.h"

#define BOUNDARY ((1ull << 32) - 1)
#define PAD (4096)
#define STACK_SIZE (1024*8192)
#define STACK_HINT (BOUNDARY - PAD - STACK_SIZE)

void __init_tls(size_t *);

static void dummy(void) {}
weak_alias(dummy, _init);

__attribute__((__weak__, __visibility__("hidden")))
extern void (*const __init_array_start)(void), (*const __init_array_end)(void);

static void dummy1(void *p) {}
weak_alias(dummy1, __init_ssp);

#define AUX_CNT 38

void __init_libc(char **envp, char *pn)
{
	size_t i, *auxv, aux[AUX_CNT] = { 0 };
	__environ = envp;
	for (i=0; envp[i]; i++);
	libc.auxv = auxv = (void *)(envp+i+1);
	for (i=0; auxv[i]; i+=2) if (auxv[i]<AUX_CNT) aux[auxv[i]] = auxv[i+1];
	__hwcap = aux[AT_HWCAP];
	__sysinfo = aux[AT_SYSINFO];
	libc.page_size = aux[AT_PAGESZ];

	if (pn) {
		__progname = __progname_full = pn;
		for (i=0; pn[i]; i++) if (pn[i]=='/') __progname = pn+i+1;
	}

	__init_tls(aux);
	__init_ssp((void *)aux[AT_RANDOM]);

	if (aux[AT_UID]==aux[AT_EUID] && aux[AT_GID]==aux[AT_EGID]
		&& !aux[AT_SECURE]) return;

	struct pollfd pfd[3] = { {.fd=0}, {.fd=1}, {.fd=2} };
#ifdef SYS_poll
	__syscall(SYS_poll, pfd, 3, 0);
#else
	__syscall(SYS_ppoll, pfd, 3, &(struct timespec){0}, 0, _NSIG/8);
#endif
	for (i=0; i<3; i++) if (pfd[i].revents&POLLNVAL)
		if (__sys_open("/dev/null", O_RDWR)<0)
			a_crash();
	libc.secure = 1;
}

static void libc_start_init(void)
{
	_init();
	uintptr_t a = (uintptr_t)&__init_array_start;
	for (; a<(uintptr_t)&__init_array_end; a+=sizeof(void(*)()))
		(*(void (**)())a)();
}

weak_alias(libc_start_init, __libc_start_init);

#ifdef __USE_DATASHIELD
struct main_args {
  int argc;
  char** argv;
  char** envp;
  int (*main)(int, char**, char**);
};

int __change_stack_start_main(void* args) {
	__libc_start_init();
  struct main_args* p = (struct main_args*) args;
  // make sure argv and enpv are all in unsafe
  char** new_argv = malloc(sizeof(char*)*p->argc);
  for (int i = 0; i < p->argc; i++) {
    new_argv[i] = malloc(strlen(p->argv[i]));
    strcpy(new_argv[i], p->argv[i]);
  }
  int n = 0;
  while (p->envp[n++]);
  n--;
  char** new_envp = malloc(sizeof(char*)*n);
  for (int i = 0; i < n; i++) {
    new_envp[i] = malloc(strlen(p->envp[i]));
    strcpy(new_envp[i], p->envp[i]);
  }
	return p->main(p->argc, new_argv, new_envp);
}

void *__ds_unsafe_stack_top;

int  __ds_unsafe_clone(void (*fn)(void*), void* arg) {
	int pid = clone(fn, 
                  __ds_unsafe_stack_top,
                  SIGCHLD | CLONE_PTRACE | CLONE_VM | CLONE_IO | CLONE_FS | CLONE_FILES,
                  arg);
  int status;
  waitpid(pid, &status, 0); 
  return WEXITSTATUS(status);
}
#endif

int __libc_start_main(int (*main)(int,char **,char **), int argc, char **argv)
{
	char **envp = argv+argc+1;

	__init_libc(envp, argv[0]);
#ifdef __USE_DATASHIELD
  __ds_init();
  void* stack_bottom = mmap((void*)STACK_HINT,
                            STACK_SIZE,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS,
                            -1,
                            0);
  if (stack_bottom == MAP_FAILED) {
    fprintf(stderr, "mapping stack failed!\n");
    assert(0);
  }

  __ds_unsafe_stack_top = (void*) (((size_t)stack_bottom) + STACK_SIZE);

  struct main_args args;
  args.argc = argc;
  args.argv = argv;
  args.envp = envp;
  args.main = main;
	//int pid = clone(__change_stack_start_main, __ds_unsafe_stack_top, SIGCHLD | CLONE_PTRACE | CLONE_VM | CLONE_IO | CLONE_FS | CLONE_FILES, (void*)&args);

  //int status;
  //waitpid(pid, &status, 0); 
  exit(__ds_unsafe_clone(__change_stack_start_main, (void*)&args));
#else
	__libc_start_init();
	/* Pass control to the application */
	exit(main(argc, argv, envp));
#endif
	return 0;
}

