#
# Makefile for musl (requires GNU make)
#
# This is how simple every makefile should be...
# No, I take that back - actually most should be less than half this size.
#
# Use config.mak to override any of the following variables.
# Do not make changes here.
#

srcdir = .
exec_prefix = /usr/local
bindir = $(exec_prefix)/bin

prefix = /usr/local/musl
includedir = $(prefix)/include
libdir = $(prefix)/lib
syslibdir = /lib

SRC_DIRS = $(addprefix $(srcdir)/,src/* crt ldso)
BASE_GLOBS = $(addsuffix /*.c,$(SRC_DIRS))
ARCH_GLOBS = $(addsuffix /$(ARCH)/*.[csS],$(SRC_DIRS))
BASE_SRCS = $(sort $(wildcard $(BASE_GLOBS)))
ARCH_SRCS = $(sort $(wildcard $(ARCH_GLOBS)))
BASE_OBJS = $(patsubst $(srcdir)/%,%.o,$(basename $(BASE_SRCS)))
ARCH_OBJS = $(patsubst $(srcdir)/%,%.o,$(basename $(ARCH_SRCS)))
REPLACED_OBJS = $(sort $(subst /$(ARCH)/,/,$(ARCH_OBJS)))
ALL_OBJS = $(addprefix obj/, $(filter-out $(REPLACED_OBJS), $(sort $(BASE_OBJS) $(ARCH_OBJS))))

LIBC_OBJS = $(filter obj/src/%,$(ALL_OBJS))
LDSO_OBJS = $(filter obj/ldso/%,$(ALL_OBJS:%.o=%.lo))
CRT_OBJS = $(filter obj/crt/%,$(ALL_OBJS))

AOBJS = $(LIBC_OBJS)
LOBJS = $(LIBC_OBJS:.o=.lo)
GENH = obj/include/bits/alltypes.h obj/include/bits/syscall.h
GENH_INT = obj/src/internal/version.h
IMPH = $(addprefix $(srcdir)/, src/internal/stdio_impl.h src/internal/pthread_impl.h src/internal/libc.h)

LDFLAGS =
LDFLAGS_AUTO =
LIBCC = -lgcc
CPPFLAGS =
CFLAGS =
CFLAGS_AUTO = -Os -pipe
CFLAGS_C99FSE = -std=c99 -ffreestanding -nostdinc 

CFLAGS_ALL = $(CFLAGS_C99FSE)
CFLAGS_ALL += -D_XOPEN_SOURCE=700 -I$(srcdir)/arch/$(ARCH) -I$(srcdir)/arch/generic -Iobj/src/internal -I$(srcdir)/src/internal -Iobj/include -I$(srcdir)/include
CFLAGS_ALL += $(CPPFLAGS) $(CFLAGS_AUTO)
# These flags are selectively re-added for certain files below.
CFLAGS_ALL += $(filter-out -fsanitize=safe-stack -mseparate-stack-seg,$(CFLAGS))

LDFLAGS_ALL = $(LDFLAGS_AUTO) $(LDFLAGS)

#AR      = $(CROSS_COMPILE)ar
AR	= $(HOME)/research/datashield/ds_sysroot_debug/bin/llvm-ar
#RANLIB  = $(CROSS_COMPILE)ranlib
RANLIB	= $(HOME)/research/datashield/ds_sysroot_debug/bin/llvm-ranlib
INSTALL = $(srcdir)/tools/install.sh

ARCH_INCLUDES = $(wildcard $(srcdir)/arch/$(ARCH)/bits/*.h)
GENERIC_INCLUDES = $(wildcard $(srcdir)/arch/generic/bits/*.h)
INCLUDES = $(wildcard $(srcdir)/include/*.h $(srcdir)/include/*/*.h)
ALL_INCLUDES = $(sort $(INCLUDES:$(srcdir)/%=%) $(GENH:obj/%=%) $(ARCH_INCLUDES:$(srcdir)/arch/$(ARCH)/%=include/%) $(GENERIC_INCLUDES:$(srcdir)/arch/generic/%=include/%))

EMPTY_LIB_NAMES = m rt pthread crypt util xnet resolv dl
EMPTY_LIBS = $(EMPTY_LIB_NAMES:%=lib/lib%.a)
CRT_LIBS = $(addprefix lib/,$(notdir $(CRT_OBJS)))
STATIC_LIBS = lib/libc.a
SHARED_LIBS =
TOOL_LIBS = lib/musl-gcc.specs
ALL_LIBS = $(CRT_LIBS) $(STATIC_LIBS) $(SHARED_LIBS) $(EMPTY_LIBS) $(TOOL_LIBS)
ALL_TOOLS = obj/musl-gcc

WRAPCC_GCC = gcc
WRAPCC_CLANG = clang

LDSO_PATHNAME = $(syslibdir)/ld-musl-$(ARCH)$(SUBARCH).so.1

-include config.mak

ifeq ($(ARCH),)

all:
	@echo "Please set ARCH in config.mak before running make."
	@exit 1

else

all: $(ALL_LIBS) $(ALL_TOOLS)

OBJ_DIRS = $(sort $(patsubst %/,%,$(dir $(ALL_LIBS) $(ALL_TOOLS) $(ALL_OBJS) $(GENH) $(GENH_INT))) obj/include)

$(ALL_LIBS) $(ALL_TOOLS) $(ALL_OBJS) $(ALL_OBJS:%.o=%.lo) $(GENH) $(GENH_INT): | $(OBJ_DIRS)

$(OBJ_DIRS):
	mkdir -p $@

obj/include/bits/alltypes.h: $(srcdir)/arch/$(ARCH)/bits/alltypes.h.in $(srcdir)/include/alltypes.h.in $(srcdir)/tools/mkalltypes.sed
	sed -f $(srcdir)/tools/mkalltypes.sed $(srcdir)/arch/$(ARCH)/bits/alltypes.h.in $(srcdir)/include/alltypes.h.in > $@

obj/include/bits/syscall.h: $(srcdir)/arch/$(ARCH)/bits/syscall.h.in
	cp $< $@
	sed -n -e s/__NR_/SYS_/p < $< >> $@

obj/src/internal/version.h: $(wildcard $(srcdir)/VERSION $(srcdir)/.git)
	printf '#define VERSION "%s"\n' "$$(cd $(srcdir); sh tools/version.sh)" > $@

obj/src/internal/version.o obj/src/internal/version.lo: obj/src/internal/version.h

obj/crt/rcrt1.o obj/ldso/dlstart.lo obj/ldso/dynlink.lo: $(srcdir)/src/internal/dynlink.h $(srcdir)/arch/$(ARCH)/reloc.h

obj/crt/crt1.o obj/crt/scrt1.o obj/crt/rcrt1.o obj/ldso/dlstart.lo: $(srcdir)/arch/$(ARCH)/crt_arch.h

obj/crt/rcrt1.o: $(srcdir)/ldso/dlstart.c

obj/crt/Scrt1.o obj/crt/rcrt1.o: CFLAGS_ALL += -fPIC

obj/crt/$(ARCH)/crti.o: $(srcdir)/crt/$(ARCH)/crti.s

obj/crt/$(ARCH)/crtn.o: $(srcdir)/crt/$(ARCH)/crtn.s

OPTIMIZE_SRCS = $(wildcard $(OPTIMIZE_GLOBS:%=$(srcdir)/src/%))
$(OPTIMIZE_SRCS:$(srcdir)/%.c=obj/%.o) $(OPTIMIZE_SRCS:$(srcdir)/%.c=obj/%.lo): CFLAGS += -O3

MEMOPS_SRCS = src/string/memcpy.c src/string/memmove.c src/string/memcmp.c src/string/memset.c
$(MEMOPS_SRCS:%.c=obj/%.o) $(MEMOPS_SRCS:%.c=obj/%.lo): CFLAGS_ALL += $(CFLAGS_MEMOPS)

NOSSP_SRCS = $(wildcard crt/*.c) \
	src/env/__libc_start_main.c src/env/__init_tls.c \
	src/env/__stack_chk_fail.c \
	src/thread/__set_thread_area.c src/thread/$(ARCH)/__set_thread_area.c \
	src/string/memset.c src/string/$(ARCH)/memset.c \
	src/string/memcpy.c src/string/$(ARCH)/memcpy.c \
	ldso/dlstart.c ldso/dynlink.c
$(NOSSP_SRCS:%.c=obj/%.o) $(NOSSP_SRCS:%.c=obj/%.lo): CFLAGS_ALL += $(CFLAGS_NOSSP)

SEP_STACK_SEG_OBJS = $(filter-out $(CRT_OBJS) obj/ldso/dlstart.o,$(ALL_OBJS))
# The safestack attribute will be selectively forced within these files below.
SAFE_STACK_OBJS = $(filter-out \
	$(addprefix obj/src/env/__init_tls.,o lo) \
	$(addprefix obj/src/env/__libc_start_main.,o lo) \
	$(addprefix obj/src/internal/safe_stack.,o lo), \
	$(SEP_STACK_SEG_OBJS))

ifeq ($(SAFE_STACK),yes)

CFLAGS_ALL += -DSAFE_STACK=1

define FORCE_ATTR =
 $(eval $(addprefix obj/$(1).,o lo): CFLAGS_ALL += $(foreach func,$(2),-mllvm -force-attribute=$(func):$(FORCED_ATTR)))
endef

FORCED_ATTR = noinline

# The no_sanitize attribute will only take effect if there are no inlined
# functions that lack the attribute. That is why noinline is applied to the
# following functions that are called from functions with the no_sanitize
# attribute.
$(call FORCE_ATTR,ldso/dynlink, \
	__pthread_self a_crash decode_dyn decode_vec find_sym \
	kernel_mapped_dso load_deps load_preload make_global makefuncdescs \
	map_library reclaim_gaps reloc_all search_vec update_tls_size)
$(call FORCE_ATTR,src/env/__init_tls, a_crash)
$(call FORCE_ATTR,src/env/__libc_start_main, a_crash)
$(call FORCE_ATTR,src/internal/safe_stack, __pthread_self a_crash)

FORCED_ATTR = safestack

# Since __init_tp switches to a new thread control block, it is necessary to
# avoid accessing the unsafe stack pointer from the time that switch occurs
# until the unsafe_stack_ptr field in the new thread control block has been
# initialized.  That is why only the following function in __init_tls.c is
# instrumented with SafeStack.
$(call FORCE_ATTR,src/env/__init_tls, __copy_tls)
$(call FORCE_ATTR,src/env/__libc_start_main, libc_start_init)
$(call FORCE_ATTR,src/internal/safe_stack, \
	__safestack_init_thread __safestack_pthread_exit)

$(SAFE_STACK_OBJS) $(SAFE_STACK_OBJS:%.o=%.lo): CFLAGS_ALL += -fsanitize=safe-stack
obj/src/env/getenv.o obj/src/env/getenv.lo: CFLAGS_ALL += -flto

endif

ifeq ($(ARCH)$(SUBARCH),i386ss)

CFLAGS_ALL += -DSEP_STACK_SEG=1

$(SEP_STACK_SEG_OBJS) $(SEP_STACK_SEG_OBJS:%.o=%.lo): CFLAGS_ALL += -mseparate-stack-seg

# The following function is run prior to segment restrictions being activated.
# It contains code that compiles to an instruction that may access either a
# stack allocation or a non-stack allocation depending on a condition.
# The X86FixupSeparateStack pass in LLVM does not support such code, since
# simply directing the affected memory operand to either DS or SS could result
# in an invalid memory access if DS and SS have different base addresses.
# Specifying this attribute causes the X86FixupSeparateStack pass to not
# process this function.  An alternative approach could have been to direct such
# memory operands to SS if DS and SS have the same base address, which is the
# way that musl configures the segments.  However, such compiler support has not
# been implemented.  Regardless, adding segment override prefixes to functions
# that only run with a flat memory model is superfluous.
obj/ldso/dynlink.lo: CFLAGS_ALL += -mllvm -sep-stk-seg-flat-mem-func=__dls3

endif

$(CRT_OBJS): CFLAGS_ALL += -DCRT

$(LOBJS) $(LDSO_OBJS): CFLAGS_ALL += -fPIC

CC_CMD = $(CC) $(CFLAGS_ALL) -c -o $@ $<

# Choose invocation of assembler to be used
ifeq ($(ADD_CFI),yes)
	AS_CMD = LC_ALL=C awk -f $(srcdir)/tools/add-cfi.common.awk -f $(srcdir)/tools/add-cfi.$(ARCH).awk $< | $(CC) $(CFLAGS_ALL) -x assembler -c -o $@ -
else
	AS_CMD = $(CC_CMD)
endif

obj/%.o: $(srcdir)/%.s
	$(AS_CMD)

obj/%.o: $(srcdir)/%.S
	$(CC_CMD)

obj/%.o: $(srcdir)/%.c $(GENH) $(IMPH)
	$(CC_CMD)

obj/%.lo: $(srcdir)/%.s
	$(AS_CMD)

obj/%.lo: $(srcdir)/%.S
	$(CC_CMD)

obj/%.lo: $(srcdir)/%.c $(GENH) $(IMPH)
	$(CC_CMD)

lib/libc.so: $(LOBJS) $(LDSO_OBJS)
	$(CC) $(CFLAGS_ALL) $(LDFLAGS_ALL) -nostdlib -shared \
	-Wl,-e,_dlstart -o $@ $(LOBJS) $(LDSO_OBJS) $(LIBCC)

lib/libc.a: $(AOBJS)
	rm -f $@
	$(AR) rc $@ $(AOBJS)
	$(RANLIB) $@

$(EMPTY_LIBS):
	rm -f $@
	$(AR) rc $@

lib/%.o: obj/crt/$(ARCH)/%.o
	cp $< $@

lib/%.o: obj/crt/%.o
	cp $< $@

lib/musl-gcc.specs: $(srcdir)/tools/musl-gcc.specs.sh config.mak
	sh $< "$(includedir)" "$(libdir)" "$(LDSO_PATHNAME)" > $@

obj/musl-gcc: config.mak
	printf '#!/bin/sh\nexec "$${REALGCC:-$(WRAPCC_GCC)}" "$$@" -specs "%s/musl-gcc.specs"\n' "$(libdir)" > $@
	chmod +x $@

obj/%-clang: $(srcdir)/tools/%-clang.in config.mak
	sed -e 's!@CC@!$(WRAPCC_CLANG)!g' -e 's!@PREFIX@!$(prefix)!g' -e 's!@INCDIR@!$(includedir)!g' -e 's!@LIBDIR@!$(libdir)!g' -e 's!@LDSO@!$(LDSO_PATHNAME)!g' $< > $@
	chmod +x $@

$(DESTDIR)$(bindir)/%: obj/%
	$(INSTALL) -D $< $@

$(DESTDIR)$(libdir)/%.so: lib/%.so
	$(INSTALL) -D -m 755 $< $@

$(DESTDIR)$(libdir)/%: lib/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/bits/%: $(srcdir)/arch/$(ARCH)/bits/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/bits/%: $(srcdir)/arch/generic/bits/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/bits/%: obj/include/bits/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(includedir)/%: $(srcdir)/include/%
	$(INSTALL) -D -m 644 $< $@

$(DESTDIR)$(LDSO_PATHNAME): $(DESTDIR)$(libdir)/libc.so
	$(INSTALL) -D -l $(libdir)/libc.so $@ || true

install-libs: $(ALL_LIBS:lib/%=$(DESTDIR)$(libdir)/%) $(if $(SHARED_LIBS),$(DESTDIR)$(LDSO_PATHNAME),)

install-headers: $(ALL_INCLUDES:include/%=$(DESTDIR)$(includedir)/%)

install-tools: $(ALL_TOOLS:obj/%=$(DESTDIR)$(bindir)/%)

install: install-libs install-headers install-tools

musl-git-%.tar.gz: .git
	 git --git-dir=$(srcdir)/.git archive --format=tar.gz --prefix=$(patsubst %.tar.gz,%,$@)/ -o $@ $(patsubst musl-git-%.tar.gz,%,$@)

musl-%.tar.gz: .git
	 git --git-dir=$(srcdir)/.git archive --format=tar.gz --prefix=$(patsubst %.tar.gz,%,$@)/ -o $@ v$(patsubst musl-%.tar.gz,%,$@)

endif

clean:
	rm -rf obj lib

distclean: clean
	rm -f config.mak

.PHONY: all clean install install-libs install-headers install-tools
