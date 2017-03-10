#ifdef __USE_DATASHIELD
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <netdb.h>
#include <locale.h>
#include "dlmalloc.h"

//#define DEBUG_MODE
//
//

typedef struct {void *base, *last;} __ds_bounds_t;
typedef struct {
  //void *ptrAddr;
  __ds_bounds_t bounds;
} __ds_table_entry;


#ifdef DEBUG_MODE
#define DEBUG(...) (fprintf(stderr, "[DC Runtime] " __VA_ARGS__))
//#define DEBUG(...)
#define DEBUG_ASSERT(x) (assert(x))
#else
#define DEBUG(...)
#define DEBUG_ASSERT(x)
#endif

#define __CTYPE_B_LOC_SIZE (384)
#define BOUNDARY ((1ull << 32) - 1)
#define PAD (4096)
#define UNSAFE_STACK_SIZE (1024*8192)
#define UNSAFE_STACK_HINT (BOUNDARY - PAD - UNSAFE_STACK_SIZE)

#define UNSAFE_HEAP_SIZE (1ull << 31)

#define SAFE_HEAP_SIZE (1ull << 32ull)
#define METADATA_TABLE_SIZE (2ull*SAFE_HEAP_SIZE)
#define SAFE_REGION_SIZE (SAFE_HEAP_SIZE + METADATA_TABLE_SIZE)
#define SAFE_HEAP_HINT (BOUNDARY + PAD)

#define N_ARG_ENTRIES (128)
#define N_TABLE_ENTRIES (METADATA_TABLE_SIZE / sizeof(__ds_table_entry))
//#define GLOBAL_RESERVE (32768ull) // hopefully we dont have more than 8*32768 bytes of globals
#define GLOBAL_RESERVE (32768ull*100) // hopefully we dont have more than 8*32768 bytes of globals
//#define N_TABLE_ENTRIES (1ull << 27)


//void* unsafe_stack_bottom;
void* unsafe_heap;
void* safe_heap;
mspace unsafe_region, safe_region;
//size_t __ds_table_count = 0;
size_t __ds_num_masks = 0;
__attribute__((visibility("default")))
size_t __ds_num_bounds_checks = 0;
size_t __ds_num_safe_heap_allocs = 0;
size_t __ds_num_unsafe_heap_allocs = 0;


__ds_table_entry *__ds_table = 0;

#define __ds_invalid_bounds ((__ds_bounds_t) { (void*)~0x0ull, (void*)(0x0ull)} )

#define __ds_empty_bounds ((__ds_bounds_t) { (void*)0x0ull, (void*)(0x0ull)} )

#define __ds_infinite_bounds ((__ds_bounds_t) { (void*)0x0ull, (void*)(~0x0ull)} )

#define __ds_unsafe_region_bounds ((__ds_bounds_t) { (void*)0x0ull, (void*)BOUNDARY} )

__ds_bounds_t __ds_fn_args_array[N_ARG_ENTRIES]; // TODO allocate this in the safe region

//__attribute__((visibility("default")))
//char** __ds_environ;

void __ds_debug_bounds_sanity_check(__ds_bounds_t bounds) {
  // the only valid bounds values are empty (for null pointers)
  // and somewhere within the safe region
  if ((bounds.base < (void*)BOUNDARY || bounds.last < (void*)BOUNDARY)
      && !(bounds.base == __ds_empty_bounds.base && bounds.last == __ds_empty_bounds.last)) {
    fprintf(stderr, "invalid bounds value found!\n");
    assert(0);
  }
}

size_t __ds_hash(void* ptr) {
  DEBUG("ptr: %p\n", ptr);
  DEBUG_ASSERT((size_t)ptr >= BOUNDARY);
  size_t hash = 0;
  if (ptr < __ds_table) {
      hash = ((size_t) ptr - BOUNDARY) / 8;
      DEBUG("hash: %li\n", hash);
      DEBUG_ASSERT(hash < GLOBAL_RESERVE);
  } else {
      hash = ((size_t) ptr  - ((size_t) __ds_table + sizeof(__ds_table_entry)*N_TABLE_ENTRIES)) / 8ull + GLOBAL_RESERVE;
      //size_t hash = ((size_t) __ds_table - (size_t)ptr ) / 8 ;
  }
  DEBUG("hash: %lu\n", hash);
  DEBUG_ASSERT(hash < N_TABLE_ENTRIES);
  return hash;
}

__attribute__((visibility("default")))
void __ds_set_fn_arg_bounds_debug(size_t i, __ds_bounds_t bounds, const char* msg) {
  DEBUG("(set fn arg) @ %lu <= [%p, %p] from: %s\n", i, bounds.base, bounds.last, msg);
#ifdef DEBUG_MODE
  __ds_debug_bounds_sanity_check(bounds);
#endif
  __ds_fn_args_array[i] = bounds;
}

// mallocs
__attribute__((visibility("default")))
void* __ds_unsafe_malloc(size_t n) {
#ifdef DEBUG_MODE
  ++__ds_num_unsafe_heap_allocs;
#endif
  void* ptr = mspace_malloc(unsafe_region, n);
  memset(ptr, 0, n);
  DEBUG("unsafe malloc: %li@%p\n", n, ptr);
  return ptr;
}
__attribute__((visibility("default")))
void* __ds_safe_malloc(size_t n) {
#ifdef DEBUG_MODE
  ++__ds_num_safe_heap_allocs;
#endif
  void* ptr = mspace_malloc(safe_region, n);
  DEBUG("safe malloc: %li@%p\n", n, ptr);
  return ptr;
}
 __attribute__((visibility("default")))
void* __ds_debug_safe_malloc(size_t n, char* msg, size_t id) {
  ++__ds_num_safe_heap_allocs;
  void* ptr = mspace_malloc(safe_region, n);
  DEBUG("safe malloc: %li@%p. from: %s. ID: %li\n", n, ptr, msg, id);
  return ptr;
}

__attribute__((visibility("default")))
void* __ds_debug_safe_alloc(size_t n, size_t id) {
  void* ptr = mspace_malloc(safe_region, n);
  DEBUG("safe alloc: %li@%p. ID: %li\n", n, ptr, id);
  return ptr;
}

// frees
__attribute__((visibility("default")))
void __ds_unsafe_free(void* ptr) {
  // we might have garbage in the upper bits when using prefixing
  //ptr = (void*) ((size_t)(ptr) & ((1ull << 32) - 1));
  DEBUG("unsafe free: %p\n", ptr);
  DEBUG_ASSERT((size_t)ptr < BOUNDARY);
  mspace_free(unsafe_region, ptr);
}
// frees
__attribute__((visibility("default")))
void __ds_safe_free(void* ptr) {
  DEBUG("safe free: %p\n", ptr);
  if (ptr == 0) { return; }
  DEBUG_ASSERT((size_t)ptr > BOUNDARY);
  mspace_free(safe_region, ptr);
}

__attribute__((visibility("default")))
void __ds_safe_free_debug(void* ptr, size_t id) {
  DEBUG("safe free: %p.  ID:%li \n", ptr, id);
  if (ptr == 0) { return; } // it's valid to call free on a nullptr (nothing happens)
  DEBUG_ASSERT(ptr && (size_t)ptr > BOUNDARY);
  mspace_free(safe_region, ptr);
}

 __attribute__((visibility("default")))
void __ds_safe_dealloc(void* ptr, size_t id) {
  DEBUG("safe dealloc: %p. ID: %li\n", ptr, id);
  if (ptr == 0) { return; } // it's valid to call free on a nullptr (nothing happens)
  DEBUG_ASSERT((size_t)ptr > BOUNDARY);
  mspace_free(safe_region, ptr);
}

// callocs
__attribute__((visibility("default")))
void* __ds_unsafe_calloc(size_t n, size_t elem_size) {
#ifdef DEBUG_MODE
  ++__ds_num_unsafe_heap_allocs;
#endif
  void* ptr = mspace_calloc(unsafe_region, n, elem_size);
  DEBUG("unsafe calloc: %lix%li@%p\n", n, elem_size, ptr);
  return ptr;
}
__attribute__((visibility("default")))
void* __ds_safe_calloc(size_t n, size_t elem_size) {
#ifdef DEBUG_MODE
  ++__ds_num_safe_heap_allocs;
#endif
  void* ptr = mspace_calloc(safe_region, n, elem_size);
  DEBUG("safe calloc: %lix%li@%p\n", n, elem_size, ptr);
  return ptr;
}
__attribute__((visibility("default")))
void* __ds_debug_safe_calloc(size_t n, size_t elem_size, char* msg) {
  ++__ds_num_safe_heap_allocs;
  void* ptr = mspace_calloc(safe_region, n, elem_size);
  DEBUG("safe calloc: %lix%li@%p. from: %s\n", n, elem_size, ptr, msg);
  return ptr;
}

// reallocs
__attribute__((visibility("default")))
void* __ds_unsafe_realloc(void* ptr, size_t n) {
#ifdef DEBUG_MODE
  ++__ds_num_unsafe_heap_allocs;
#endif
  DEBUG("unsafe realloc requested\n");
  void* new_ptr = mspace_realloc(unsafe_region, ptr, n);
  DEBUG("unsafe realloc: %p:%li => %p\n", ptr, n, new_ptr);
  return new_ptr;
}
__attribute__((visibility("default")))
void* __ds_safe_realloc(void* ptr, size_t n) {
#ifdef DEBUG_MODE
  ++__ds_num_safe_heap_allocs;
#endif
  DEBUG("safe realloc requested: @%p x %li\n", ptr, n);
  void* new_ptr = mspace_realloc(safe_region, ptr, n);
  DEBUG("safe realloc: %p:%li => %p\n", ptr, n, new_ptr);
  return new_ptr;
}

// strdups
__attribute__((visibility("default")))
char* __ds_unsafe_strdup(char* orig) {
  int n = strlen(orig);
  char *dup = (char *)__ds_unsafe_malloc(n);
  strcpy(dup, orig);
  DEBUG("(unsafe strdup) %p => %s to %p\n", orig, orig, dup);
#ifdef DEBUG_MODE
  ++__ds_num_unsafe_heap_allocs;
#endif
  return dup;
} 
__attribute__((visibility("default")))
char* __ds_safe_strdup(char* orig) {
  // TODO fix these bounds!!
  //char const * where = "__dc_unsafe_strdup";
  //__dc_bounds_t orig_bounds = __dc_get_fn_arg_bounds_debug(1, (char*) where);
  //__dc_check_bounds_debug(orig_bounds, orig, sizeof(char*), (char*) where);
#ifdef DEBUG_MODE
  ++__ds_num_safe_heap_allocs;
#endif

  int n = strlen(orig);
  char *dup = (char *)__ds_safe_malloc(n);
  strcpy(dup, orig);
  DEBUG("(safe strdup) %p => %s to %p\n", orig, orig, dup);
  //__dc_bounds_t new_bounds = {dup, ((char*)dup)+n-1};
  //__dc_set_fn_arg_bounds_debug(0, new_bounds, (char*) where);
  return dup;
} 

//// C functions that allocate
//__attribute__((visibility("default")))
//void* __ds_unsafe_localtime(void* timep) {
//  struct tm* safe = localtime((const time_t*)timep);
//  struct tm* unsafe = (struct tm*)__ds_unsafe_malloc(sizeof(struct tm));
//  memcpy(unsafe, safe, sizeof(struct tm));
//  return unsafe;
//}
//
//__attribute__((visibility("default")))
//void* __ds_unsafe_gmtime(void* timep) {
//  struct tm* safe = gmtime((const time_t*)timep);
//  struct tm* unsafe = (struct tm*)__ds_unsafe_malloc(sizeof(struct tm));
//  memcpy(unsafe, safe, sizeof(struct tm));
//  return unsafe;
//}
//
//__attribute__((visibility("default")))
//void* __ds_safe_gmtime(void* timep) {
//  struct tm* orig = gmtime((const time_t*)timep);
//  struct tm* safe = (struct tm*)__ds_safe_malloc(sizeof(struct tm));
//  memcpy(safe, orig, sizeof(struct tm));
//#ifdef DEBUG_MODE
//    __ds_bounds_t bounds = { safe, ((char*)safe) + sizeof(struct tm) - 1 };
//    __ds_set_fn_arg_bounds_debug(0, bounds, "__ds_safe_gmtime");
//#endif
//  return safe;
//}
//
//// init functions
//__attribute__((visibility("default")))
//char** __ds_copy_argv_to_unsafe_heap(int argc, char** argv) {
//  char** newArgv = (char**)__ds_unsafe_malloc(sizeof(char**)*(argc+1));
//  newArgv[argc] = 0;
//  for (int i = 0; i < argc; ++i) {
//    int sz = strlen(argv[i])+1;
//    newArgv[i] = (char*)__ds_unsafe_malloc(sz);
//    strncpy(newArgv[i], argv[i], sz);
//  }
//  return newArgv;
//}
//__attribute__((visibility("default")))
//int __ds_get_exit_code_from_status(int status) {
//  if (WIFSIGNALED(status)) {
//    fprintf(stderr, "clone died from a signal: %i\n", WTERMSIG(status));
//    assert(0 && "status wasn't normal exit\n");
//  }
//  if (!WIFEXITED(status)) {
//    fprintf(stderr, "status: %i\n", status);
//    assert(WIFEXITED(status) && "status wasn't normal exit\n");
//  }
//  return WEXITSTATUS(status);
//}
//
////extern "C" __attribute__((visibility("default")))
////void* __ds_get_unsafe_stack_top() {
////  return (void*)((size_t)unsafe_stack_bottom + UNSAFE_STACK_SIZE);
////}
//
//__attribute__((visibility("default")))
//int __ds_waitpid(pid_t pid) {
//  int status;
//  DEBUG("pid: %i\n", pid);
//  pid_t rv = waitpid(pid, &status, 0);
//  if (pid != rv) {
//    if (WIFSIGNALED(status)) {
//      fprintf(stderr,"childed was signaled to death: %i \n", WTERMSIG(status));
//      assert(0);
//    }
//  }
//  if (!WIFEXITED(status)) {
//    fprintf(stderr,"didnt exit normally!: %i \n", status);
//    assert(0);
//  }
//  DEBUG("exit status: %i\n", WEXITSTATUS(status));
//  return WEXITSTATUS(status);
//}
//
//__attribute__((visibility("default")))
void* __ds_unsafe_mmap(void* addr, size_t length, int prot, int flags,
    int fd, off_t offset) {
  DEBUG("addr: %p\n", addr);
  DEBUG("length: %li\n", length);
  DEBUG("prot: %i\n", prot);
  DEBUG("flags: %i\n", flags);
  DEBUG("fd: %i\n", flags);
  DEBUG("offset: %li\n", offset);
  void* rv =  mmap(addr, length, prot, flags | MAP_32BIT, fd, offset);
  if (rv == MAP_FAILED) {
    DEBUG("ERROR: %d - %s\n", errno, strerror(errno));
  }
  return rv;
}

//__attribute__((visibility("default")))
//const unsigned short ** __ds_unsafe_ctype_b_loc(void) {
//  const static unsigned short **obj = 0;
//  if (!obj) {
//    const unsigned short ** loc = __ctype_b_loc();
//    obj = (const unsigned short**)__ds_unsafe_malloc(sizeof(unsigned short*));
//    unsigned short* arr = (unsigned short*)__ds_unsafe_malloc(sizeof(unsigned short)* __CTYPE_B_LOC_SIZE);
//    memcpy(arr, &(*loc)[-128], sizeof(unsigned short)*__CTYPE_B_LOC_SIZE);
//    *obj = arr+128;
//  }
//  return obj;
//}
//
//__attribute__((visibility("default")))
//const int** __ds_unsafe_ctype_toupper_loc(void) {
//  const static int **obj = 0;
//  if (!obj) {
//    const int ** loc = __ctype_toupper_loc();
//    obj = (const int**)__ds_unsafe_malloc(sizeof(int*));
//    int* arr = (int*)__ds_unsafe_malloc(sizeof(int)* __CTYPE_B_LOC_SIZE);
//    memcpy(arr, &(*loc)[-128], sizeof(int)*__CTYPE_B_LOC_SIZE);
//    *obj = arr+128;
//  }
//  return obj;
//}
//
//__attribute__((visibility("default")))
//const int ** __ds_unsafe_ctype_tolower_loc(void) {
//  const int **obj = 0;
//  if (!obj) {
//    const int ** loc = __ctype_tolower_loc();
//    obj = (const int**)__ds_unsafe_malloc(sizeof(int*));
//    int* arr = (int*)__ds_unsafe_malloc(sizeof(int)* __CTYPE_B_LOC_SIZE);
//    memcpy(arr, &(*loc)[-128], sizeof(int)*__CTYPE_B_LOC_SIZE);
//    *obj = arr+128;
//  }
//  return obj;
//}
//

__attribute__((visibility("default")))
__ds_bounds_t __ds_get_bounds_debug(void* ptrAddr, char* msg, size_t id) {
  DEBUG("(get bounds: %li) @ %p => ", id, ptrAddr);
  size_t hash = __ds_hash(ptrAddr);
  __ds_bounds_t bounds = __ds_table[hash].bounds;
  DEBUG("[%p,%p)  from : %s.\n", bounds.base, bounds.last,  msg, id);
#ifdef DEBUG_MODE
  __ds_debug_bounds_sanity_check(bounds);
#endif
  return bounds;
  //return __ds_invalid_bounds; // not found
}

__attribute__((visibility("default")))
__ds_bounds_t __ds_get_fn_arg_bounds_debug(size_t i, char* msg, size_t id) {
  __ds_bounds_t bounds = __ds_fn_args_array[i];
  DEBUG("(get fn args ID:%li) @ %li => [%p, %p). from: %s\n", id, i, bounds.base, bounds.last, msg);
#ifdef DEBUG_MODE
  if (bounds.base == __ds_invalid_bounds.base && bounds.last == __ds_invalid_bounds.last) {
    fprintf(stderr, "we get the same fn arg bounds twice with out setting!\n");
    assert(0);
  }
#endif
  __ds_fn_args_array[i] = __ds_invalid_bounds;
  return bounds;
}

__attribute__((visibility("default")))
void __ds_set_bounds_debug(void *ptrAddr, __ds_bounds_t bounds, const char* msg, size_t id)
{
  DEBUG("(set bounds) @ %p <= [%p, %p]. ID: %li. from: %s\n", ptrAddr, (void*)bounds.base, (void*)bounds.last, id, msg);
#ifdef DEBUG_MODE
  __ds_debug_bounds_sanity_check(bounds);
#endif
  size_t hash = __ds_hash(ptrAddr);
  __ds_table[hash].bounds = bounds;
}

__attribute__((visibility("default")))
//__attribute__((constructor(0)))
void __ds_init() {
  //unsafe_stack_bottom = mmap((void*)UNSAFE_STACK_HINT,
  //                    UNSAFE_STACK_SIZE,
  //                    PROT_READ | PROT_WRITE,
  //                    //MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, // it seems like MAP_GROWNSDOWN doesn't do anything?
  //                    MAP_PRIVATE | MAP_ANONYMOUS,
  //                    -1,
  //                    0);
  //if (unsafe_stack_bottom == MAP_FAILED) {
  //  fprintf(stderr, "mapping failed!\n");
  //  assert(0);
  //} else {
  //  assert((size_t)unsafe_stack_bottom+UNSAFE_STACK_SIZE < BOUNDARY);
  //  DEBUG("unsafe stack top: %p\n", (char*)unsafe_stack_bottom + UNSAFE_STACK_SIZE);
  //  DEBUG("unsafe stack bottom: %p\n", (char*)unsafe_stack_bottom);
  //}

  size_t UNSAFE_HEAP_HINT = UNSAFE_STACK_HINT - PAD - UNSAFE_HEAP_SIZE;
  unsafe_heap = mmap((void*)UNSAFE_HEAP_HINT,
                      UNSAFE_HEAP_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);
  if (unsafe_heap == MAP_FAILED) {
    fprintf(stderr, "mapping failed!\n");
    assert(0);
  } else {
    assert((size_t)unsafe_heap+UNSAFE_HEAP_SIZE < BOUNDARY);
    DEBUG("unsafe heap top: %p\n", (char*)unsafe_heap + UNSAFE_HEAP_SIZE);
  }

  unsafe_region = create_mspace_with_base(unsafe_heap, UNSAFE_HEAP_SIZE, 0);

  safe_heap = mmap((void*)SAFE_HEAP_HINT,
                      SAFE_REGION_SIZE,
                      PROT_READ | PROT_WRITE,
                      //MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, // it seems like MAP_GROWNSDOWN doesn't do anything?
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);
  if (safe_heap == MAP_FAILED) {
    fprintf(stderr, "mapping failed!\n");
    assert(0);
  }
  safe_region = create_mspace_with_base(safe_heap, SAFE_HEAP_SIZE, 0);

  __ds_table = (__ds_table_entry*) __ds_safe_malloc(sizeof(__ds_table_entry) * N_TABLE_ENTRIES);

}
__attribute__((visibility("default")))
void __ds_abort_debug(__ds_bounds_t bounds, void* ptr, void* bottom, char* msg, size_t id) {
  fprintf(stderr, "ABORTING! ID: %li from: %s\n", id, msg);
  void* base = bounds.base;
  void* last = bounds.last;
  if (ptr < base) {
    fprintf(stderr, "ptr: %p < base: %p\n", ptr, base);
  }
  if (bottom > last) {
    fprintf(stderr, "bottom: %p > last: %p\n", bottom, last);
  }
  abort();
}

__attribute__((visibility("default")))
void __ds_metadata_copy_debug(unsigned char* dst, unsigned char* src, size_t size, size_t id) {
  // when we call memcpy on a struct, we need to copy the member pointer bounds as well
  // we only care about copying pointers
  DEBUG("(metadata copy: %lu) %p <= %p x %li\n", id, dst, src, size);
  // hack because we don't move global variables
  //if ((size_t)src < BOUNDARY) { return; }
  size_t n_ptrs = size/sizeof(void*);
  for (size_t i = 0; i < n_ptrs; ++i) {
    size_t dst_hash = __ds_hash((void*)(dst+i*sizeof(void*)));
    size_t src_hash = __ds_hash((void*)(src+i*sizeof(void*)));
    DEBUG("dst hash: %li, src hash: %li\n", dst_hash, src_hash);
    assert(dst_hash > 0);
    assert(src_hash > 0);
    assert(dst_hash < N_TABLE_ENTRIES);
    assert(src_hash < N_TABLE_ENTRIES);
    DEBUG("(metadata copy) %p <= %p : [%p, %p)\n",
          dst+i*sizeof(void*),
          src+i*sizeof(void*),
          __ds_table[src_hash].bounds.base,
          __ds_table[src_hash].bounds.last);
    //__ds_table[dst_hash].ptrAddr = __ds_table[src_hash].ptrAddr;
    __ds_table[dst_hash].bounds.base = __ds_table[src_hash].bounds.base;
    __ds_table[dst_hash].bounds.last = __ds_table[src_hash].bounds.last;
  }
  return;

}

__attribute__((visibility("default")))
void __ds_metadata_copy(unsigned char* dst, unsigned char* src, size_t size) {
  // when we call memcpy on a struct, we need to copy the member pointer bounds as well
  // we only care about copying pointers
  DEBUG("(metadata copy) %p <= %p x %li\n", dst, src, size);
  // hack because we don't move global variables
  //if ((size_t)src < BOUNDARY) { return; }
  size_t n_ptrs = size/sizeof(void*);
  for (size_t i = 0; i < n_ptrs; ++i) {
    size_t dst_hash = __ds_hash((void*)(dst+i*sizeof(void*)));
    size_t src_hash = __ds_hash((void*)(src+i*sizeof(void*)));
    assert(dst_hash > 0);
    assert(src_hash > 0);
    assert(dst_hash < N_TABLE_ENTRIES);
    assert(src_hash < N_TABLE_ENTRIES);
    DEBUG("dst hash: %li, src hash: %li\n", dst_hash, src_hash);
    DEBUG("(metadata copy) %p <= %p : [%p, %p)\n",
          dst+i*sizeof(void*),
          src+i*sizeof(void*),
          __ds_table[src_hash].bounds.base,
          __ds_table[src_hash].bounds.last);
    //__ds_table[dst_hash].ptrAddr = __ds_table[src_hash].ptrAddr;
    __ds_table[dst_hash].bounds.base = __ds_table[src_hash].bounds.base;
    __ds_table[dst_hash].bounds.last = __ds_table[src_hash].bounds.last;
  }
  return;
}

__attribute__((visibility("default")))
void __ds_safe_memcpy(unsigned char* dst, unsigned char* src, size_t size) {
  memcpy(dst, src, size);
  __ds_metadata_copy(dst, src, size);
}

// non debug bounds functions
__attribute__((visibility("default")))
void __ds_set_fn_arg_bounds(size_t i, __ds_bounds_t bounds) {
  __ds_fn_args_array[i] = bounds;
}

__attribute__((visibility("default")))
__ds_bounds_t __ds_get_fn_arg_bounds(size_t i) {
  __ds_bounds_t b = __ds_fn_args_array[i];
  __ds_fn_args_array[i] = __ds_unsafe_region_bounds;
  return b;
}

__attribute__((visibility("default")))
__ds_bounds_t __ds_get_bounds(void* ptrAddr) {
  size_t hash = __ds_hash(ptrAddr);
  __ds_bounds_t bounds = __ds_table[hash].bounds;
  return bounds;
}

__attribute__((visibility("default")))
void __ds_set_bounds(void *ptrAddr, __ds_bounds_t bounds) {
  size_t hash = __ds_hash(ptrAddr);
  //__ds_table[hash].ptrAddr = ptrAddr;
  __ds_table[hash].bounds = bounds;
}

__attribute__((visibility("default")))
void __ds_abort() {
  abort();
}

__attribute__((visibility("default")))
void* __ds_mask_debug(void* ptr, size_t id) {
    if ((unsigned long long)ptr > BOUNDARY) {
        fprintf(stderr, "we tried to mask a sensitive pointer? %p. id: %li\n", ptr, id);
        abort();
        //return ptr; // for now, don't abort
    }
    void* masked_ptr = (void*)((unsigned long long)ptr & BOUNDARY);
    ++__ds_num_masks;
    return masked_ptr;
}

//__attribute__((always_inline))  
//__attribute__((visibility("default")))
//void __ds_unsafe_bounds_check_mpx(void* ptr) {
//  //size_t bounds2[2];
//  //asm volatile ("bndmov %%bnd0, %0"
//  //    :
//  //    : "m" (bounds2)
//  //);
//  //fprintf(stderr, "(mpx bounds check): %p [%p, %p)\n", ptr, bounds2[0], bounds2[1]);
//  //asm volatile ("bndcn %0, %%bnd0"
//  //    :
//  //    : "rm" (ptr)
//  //);
//  size_t p = (size_t)ptr;
//  asm("bndcu %0, %%bnd0"
//      :
//      : "r" (p)
//  );
//  // if the bounds check fails then a signal handler will be invoked
//  // note: we only check the upper bound
//}
//
//struct addrinfo* __ds_copy_getaddrinfo_res(struct addrinfo** origRes) {
//  struct addrinfo* cur_old = *origRes;
//  struct addrinfo* cur_new, *prev_new = NULL, *new_head;
//  while (cur_old) {
//    cur_new = (struct addrinfo*) __ds_safe_malloc(sizeof(struct addrinfo));
//    memcpy(cur_new, cur_old, sizeof(struct addrinfo));
//    if (prev_new) {
//      __ds_bounds_t cur_bounds = {cur_new, ((char*)cur_new)+sizeof(struct addrinfo)-1};
//      prev_new->ai_next = cur_new;
//      __ds_set_bounds_debug(&prev_new->ai_next, cur_bounds, "__ds_copy_getaddrinfo_res", 0);
//    } else {
//      new_head = cur_new;
//    }
//    cur_old = cur_old->ai_next;
//    prev_new = cur_new;
//  }
//  return new_head;
//}
//
//struct addrinfo* __ds_copy_getaddrinfo_res_unsafe(struct addrinfo** origRes) {
//  struct addrinfo* cur_old = *origRes;
//  struct addrinfo* cur_new, *prev_new = NULL, *new_head;
//  while (cur_old) {
//    cur_new = (struct addrinfo*) __ds_unsafe_malloc(sizeof(struct addrinfo));
//    memcpy(cur_new, cur_old, sizeof(struct addrinfo));
//    if (prev_new) {
//      //__ds_bounds_t cur_bounds = {cur_new, ((char*)cur_new)+sizeof(struct addrinfo)-1};
//      prev_new->ai_next = cur_new;
//      //__ds_set_bounds_debug(&prev_new->ai_next, cur_bounds, "__ds_copy_getaddrinfo_res", 0);
//    } else {
//      new_head = cur_new;
//    }
//    cur_old = cur_old->ai_next;
//    prev_new = cur_new;
//  }
//  return new_head;
//}
//
//__attribute__((visibility("default")))
//int __ds_safe_getaddrinfo(const char* node, const char* service,
//                                              const struct addrinfo *hints, struct addrinfo **res) {
//  struct addrinfo *copy_res;
//  int rv = getaddrinfo(node, service, hints, res);
//  copy_res = __ds_copy_getaddrinfo_res(res);
//  freeaddrinfo(*res);
//  __ds_bounds_t copy_bounds = {copy_res, ((char*)copy_res) + sizeof(struct addrinfo)-1};
//  __ds_set_bounds_debug(res, copy_bounds, "__ds_copy_safe_addrinfo_list", 0);
//  *res = copy_res;
//  return rv;
//}
//
//__attribute__((visibility("default")))
//int __ds_unsafe_getaddrinfo(const char* node, const char* service,
//                                              const struct addrinfo *hints, struct addrinfo **res) {
//  struct addrinfo *copy_res;
//  int rv = getaddrinfo(node, service, hints, res);
//  copy_res = __ds_copy_getaddrinfo_res_unsafe(res);
//  freeaddrinfo(*res);
//  //__ds_bounds_t copy_bounds = {copy_res, ((char*)copy_res) + sizeof(struct addrinfo)-1};
//  //__ds_set_bounds_debug(res, copy_bounds, "__ds_copy_safe_addrinfo_list", 0);
//  *res = copy_res;
//  return rv;
//}
//
//__attribute__((visibility("default")))
//void __ds_safe_freeaddrinfo(struct addrinfo* addrinfo) {
//  // todo
//  return;
//}
//
//__attribute__((visibility("default")))
//void __ds_unsafe_freeaddrinfo(struct addrinfo* addrinfo) {
//  // todo
//  return;
//}
//
//__attribute__((visibility("default")))
//int* __ds_unsafe_errno_location(void) {
//  static int* repl = NULL;
//  if (!repl) {
//    repl = (int*)__ds_unsafe_malloc(sizeof(int));
//  }
//  int* orig = __errno_location();
//  *repl = *orig;
//  return repl;
//}

__attribute__((visibility("default")))
void __ds_bounds_check_debug(void* ptr, __ds_bounds_t bounds, const char* msg, size_t id) {
  DEBUG("(bounds check: %li) %p <= %p < %p?\n", id, bounds.base, ptr, bounds.last);
  ++__ds_num_bounds_checks;
  if (bounds.base <= ptr && ptr < bounds.last) {
      return;
  } else {
      assert(0 && "bounds check failed!\n");
  }
}

//__attribute__((visibility("default")))
//char** __ds_copy_argv_to_safe_heap(int argc, char** argv) {
//  char** newArgv = (char**)__ds_safe_malloc(sizeof(char**)*(argc+1));
//  newArgv[argc] = 0;
//  for (int i = 0; i < argc; ++i) {
//    int sz = strlen(argv[i])+1;
//    newArgv[i] = (char*)__ds_safe_malloc(sz);
//    strncpy(newArgv[i], argv[i], sz);
//    __ds_bounds_t  bounds = { newArgv[i], newArgv[i] + sz -1 };
//#ifdef DEBUG_MODE
//    __ds_set_bounds_debug(&newArgv[i], bounds, "copy safe argv debug", 0);
//#else
//    __ds_set_bounds(&newArgv[i], bounds);
//#endif
//  }
//  return newArgv;
//}

__attribute__((visibility("default")))
void __ds_print_runtime_stats() {
  fprintf(stderr, "# masks: %lu\n", __ds_num_masks);
  fprintf(stderr, "# bounds checks: %lu\n", __ds_num_bounds_checks);
  fprintf(stderr, "# safe mallocs: %lu\n", __ds_num_safe_heap_allocs);
  fprintf(stderr, "# unsafe mallocs: %lu\n", __ds_num_unsafe_heap_allocs);
}

//__attribute__((visibility("default")))
//struct lconv* __ds_unsafe_localeconv() {
//    static struct lconv* unsafe_lconv = NULL;
//    struct lconv* safe_lconv = localeconv();
//    if (unsafe_lconv == NULL) {
//      unsafe_lconv = (struct lconv*) __ds_unsafe_malloc(sizeof(struct lconv));
//    } else {
//      __ds_unsafe_free(unsafe_lconv->decimal_point);
//      __ds_unsafe_free(unsafe_lconv->thousands_sep);
//      __ds_unsafe_free(unsafe_lconv->grouping);
//      __ds_unsafe_free(unsafe_lconv->int_curr_symbol);
//      __ds_unsafe_free(unsafe_lconv->currency_symbol);
//      __ds_unsafe_free(unsafe_lconv->mon_decimal_point);
//      __ds_unsafe_free(unsafe_lconv->mon_thousands_sep);
//      __ds_unsafe_free(unsafe_lconv->mon_grouping);
//      __ds_unsafe_free(unsafe_lconv->positive_sign);
//      __ds_unsafe_free(unsafe_lconv->negative_sign);
//    }
//    unsafe_lconv->decimal_point = (char*) __ds_unsafe_strdup(safe_lconv->decimal_point);
//    unsafe_lconv->thousands_sep = (char*) __ds_unsafe_strdup(safe_lconv->thousands_sep);
//    unsafe_lconv->grouping = (char*) __ds_unsafe_strdup(safe_lconv->grouping);
//    unsafe_lconv->int_curr_symbol = (char*) __ds_unsafe_strdup(safe_lconv->int_curr_symbol);
//    unsafe_lconv->currency_symbol = (char*) __ds_unsafe_strdup(safe_lconv->currency_symbol);
//    unsafe_lconv->mon_decimal_point = (char*) __ds_unsafe_strdup(safe_lconv->mon_decimal_point);
//    unsafe_lconv->mon_thousands_sep = (char*) __ds_unsafe_strdup(safe_lconv->mon_thousands_sep);
//    unsafe_lconv->mon_grouping = (char*) __ds_unsafe_strdup(safe_lconv->mon_grouping);
//    unsafe_lconv->positive_sign = (char*) __ds_unsafe_strdup(safe_lconv->positive_sign);
//    unsafe_lconv->negative_sign = (char*) __ds_unsafe_strdup(safe_lconv->negative_sign);
//    unsafe_lconv->int_frac_digits = safe_lconv->int_frac_digits;
//    unsafe_lconv->frac_digits = safe_lconv->frac_digits;
//    unsafe_lconv->p_cs_precedes = safe_lconv->p_cs_precedes;
//    unsafe_lconv->p_sep_by_space = safe_lconv->p_sep_by_space;
//    unsafe_lconv->n_cs_precedes = safe_lconv->n_cs_precedes;
//    unsafe_lconv->n_sep_by_space = safe_lconv->n_sep_by_space;
//    unsafe_lconv->n_sign_posn = safe_lconv->n_sign_posn;
//    unsafe_lconv->p_sign_posn = safe_lconv->p_sign_posn;
//    unsafe_lconv->int_p_cs_precedes = safe_lconv->int_p_cs_precedes;
//    unsafe_lconv->int_p_sep_by_space = safe_lconv->int_p_sep_by_space;
//    unsafe_lconv->int_n_cs_precedes = safe_lconv->int_n_cs_precedes;
//    unsafe_lconv->int_n_sep_by_space = safe_lconv->int_n_sep_by_space;
//    return unsafe_lconv;
//}
//
////#define __cxa_refcounted_exception_size (116)
//#define __cxa_refcounted_exception_size (128)
//void * __ds_unsafe_cxa_allocate_exception(size_t thrown_size)
//{
//  void *ret;
//
//  //thrown_size += sizeof (__cxa_refcounted_exception);
//  thrown_size += __cxa_refcounted_exception_size;
//  //ret = malloc (thrown_size);
//  ret = __ds_unsafe_malloc(thrown_size);
//
//  if (!ret) {
//    fprintf(stderr,"we ran out of memory in an exception! punt on first down!");
//    abort();
//  }
//    //ret = emergency_pool.allocate (thrown_size);
//
//  //if (!ret)
//  //  std::terminate ();
//
//  //memset (ret, 0, sizeof (__cxa_refcounted_exception));
//  memset (ret, 0, __cxa_refcounted_exception_size);
//
//  //return (void *)((char *)ret + sizeof (__cxa_refcounted_exception));
//  void* ptr = (void *)((char *)ret + __cxa_refcounted_exception_size);
//#ifdef DEBUG
//  fprintf(stderr, "__ds_unsafe_cxa_allocate: %p\n", ptr);
//#endif
//  return ptr;
//}
//void __ds_unsafe_cxa_end_catch() {
//    // FIXME actually deallocate the exception
//    return;
//}
//
//// I need to figure out where buf is in the class/struct
//// this might not be the exact right place
//struct TempBuffer {
//    size_t _M_original_len;
//    size_t _M_len;
//    void* buf;
//};
//
//void __ds_unsafe_get_temporary_buffer(void* buf, void* begin, void* end, size_t elesize) {
//    size_t buff_size = ((size_t)end - (size_t)begin) * elesize;
//    ((struct TempBuffer*)buf)->buf = __ds_unsafe_malloc(buff_size);
//}
char* __ds_safe_strerror(int errnum) {
  char* orig = strerror(errnum);
  char* rv = __ds_safe_malloc(strlen(orig)+1);
  strcpy(rv, orig);
  __ds_bounds_t rv_bounds;
  rv_bounds.base = rv;
  rv_bounds.last = rv + strlen(orig)+1;
  __ds_set_fn_arg_bounds(0, rv_bounds);
  return rv;
}

int __ds_safe_gettimeofday(struct timeval *tv, struct timezone *tz) {
  struct timespec ts;
  __ds_bounds_t tv_bounds;
  tv_bounds.base = tv;
  tv_bounds.last = tv + sizeof(struct timeval) - 1;
  if (!tv) return 0;
  clock_gettime(CLOCK_REALTIME, &ts);
  tv->tv_sec = ts.tv_sec;
  __ds_set_bounds(&(tv->tv_sec), tv_bounds);
  tv->tv_usec = (int)ts.tv_nsec / 1000;
  __ds_set_bounds(&(tv->tv_usec), tv_bounds);
  return 0;
}

void* __ds_debug_safe_memalign(size_t alignment, size_t bytes, char* msg, size_t id) {
  void* ptr =  mspace_memalign(safe_region, alignment, bytes);
  DEBUG("safe memalign: %lix%li@%p. from: %s. ID: %li\n", alignment, bytes, ptr, msg, id);
  return ptr;
}

char** __ds_copy_argv_to_safe_heap(int argc, char** argv) {
  char** newArgv = (char**)__ds_safe_malloc(sizeof(char**)*(argc+1));
  __ds_bounds_t argv_bounds;
  argv_bounds.base = newArgv;
  argv_bounds.last = newArgv + sizeof(char**)*(argc+1);
  // why 2? I don't know the repl happens to late?
  // and argv is the second argument of main?
  __ds_set_fn_arg_bounds(2, argv_bounds);
  //__ds_set_fn_arg_bounds(0, argv_bounds); 
  newArgv[argc] = 0;
  for (int i = 0; i < argc; ++i) {
    int sz = strlen(argv[i])+1;
    newArgv[i] = (char*)__ds_safe_malloc(sz);
    __ds_bounds_t ele_bounds;
    ele_bounds.base = newArgv[i];
    ele_bounds.last = newArgv[i] + sz;
    __ds_set_bounds(&newArgv[i], ele_bounds); 
    strncpy(newArgv[i], argv[i], sz);
  }
  return newArgv;
}

__attribute__((visibility("default")))
char** __ds_copy_environ_to_safe(char** origenviron) {
  DEBUG("(copy environ): %p\n", origenviron);
  int n = 0;
  while(origenviron[n++]) {
    // do nothing
  }
  n--;
  char ** newenviron = (char**)__ds_safe_malloc(sizeof(char*)*n+1);
  newenviron[n] = 0;
  __ds_bounds_t ds_environ_bounds;
  ds_environ_bounds.base = newenviron;
  ds_environ_bounds.last = newenviron + n + 1;
  __ds_set_fn_arg_bounds(0, ds_environ_bounds);
  for (int i = 0; i < n; ++i) {
    __ds_bounds_t ele_bounds;
    DEBUG("copying: %s\n", origenviron[i]);
    int n = strlen(origenviron[i]) + 1;
    newenviron[i] = (char*)__ds_safe_malloc(n);
    strcpy(newenviron[i], origenviron[i]);
    ele_bounds.base = newenviron[i];
    ele_bounds.last = newenviron[i] + n;
  __ds_set_bounds(newenviron[i], ele_bounds);
  }
  for (int i = 0; i < n; ++i) {
    DEBUG("%s\n", newenviron[i]);
  }
  DEBUG("(copy environ): done\n");
  return newenviron;
}

__attribute__((visibility("default")))
char**  __ds_copy_environ_to_unsafe(char** origenviron) {
  DEBUG("(copy environ): %p\n", origenviron);
  int n = 0;
  while(origenviron[n++]) {
    // do nothing
  }
  n--;
  char** newenviron = (char**) __ds_unsafe_malloc(sizeof(char*)*n+1);
  newenviron[n] = 0;
  for (int i = 0; i < n; ++i) {
    DEBUG("copying: %s\n", origenviron[i]);
    // TODO we could make much better bounds
    //char const * where= "__dc_copy_environ";
    //__dc_set_fn_arg_bounds_debug(1, __dc_infinite_bounds, (char*) where);
    int n = strlen(origenviron[i]) + 1;
    newenviron[i] = (char*)__ds_unsafe_malloc(n);
    //__dc_set_fn_arg_bounds_debug(1, __dc_infinite_bounds, (char*) where);
    //__dc_set_fn_arg_bounds_debug(2, __dc_infinite_bounds, (char*) where);
    strcpy(newenviron[i], origenviron[i]);
  }
  for (int i = 0; i < n; ++i) {
    DEBUG("%s\n", newenviron[i]);
  }
  DEBUG("(copy environ): done\n");
  return newenviron;
}

#endif
