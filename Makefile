#
# Cute Makefile
# (C) 2009-2012 Ahmed S. Darwish <darwish.07@gmail.com>
#

CC	= gcc
CPP	= cpp
LD	= ld

#
# Machine-dependent C Flags:
#
# Use the AMD64 'kernel' code model for reasons stated in our
# head.S bootstrap code.
#
# Disable SSE floating point ops. They need special CPU state
# setup, or several #UD and #NM exceptions will be triggered.
#
# Do not use the AMD64 ABI 'red zone'. This was a bug that
# costed me an entire week to fix!
#
# Basically the zone is 128-byte area _below_ the stack pointer
# that can be used for temporary local state, especially for
# leaf functions.
#
# Ofcouse this red zone is disastrous to be used in the kernel
# since it's where the CPU pushes %ss, %rsp, %rflags, %cs and
# %rip on the event of invoking an interrupt handler. If used,
# it will simply lead to kernel stack corruption.
#
# Check attached docs for extra info on -mcmodel and the zone.
#
CMACH_FLAGS =				\
  -m64					\
  -mcmodel=kernel			\
  -mno-mmx				\
  -mno-sse				\
  -mno-sse2				\
  -mno-sse3				\
  -mno-3dnow				\
  -mno-red-zone

#
# GCC C dialect flags:
#
# We're a freestanding environment by ISO C99 definition:
# - Code get executed without benefit of an OS (it's a kernel).
# - Program startup and termination is implementation-defined.
# - Any library facilities outside C99 'strict conformance'
#   options are also implementation defined.
#
# Poking GCC with the 'freestanding' flag assures it won't
# presume availability of any 'hosted' facilities like libc.
#
# After using -nostdinc, we add compiler's specific includes
# back (stdarg.h, etc) using the -iwithprefix flag.
#
CDIALECT_FLAGS =			\
  -std=gnu99				\
  -ffreestanding			\
  -fno-builtin				\
  -nostdlib				\
  -nostdinc				\
  -iwithprefix include			\
  -I include/

#
# C Optimization flags:
#
# Use -O3 to catch any weird bugs early on
#
# Note-1! Fallback to -O2 at official releases
# Note-2! Shouldn't we disable strict aliasing?
#
COPT_FLAGS =				\
  -O3					\
  -pipe

#
# Warnings request and dismissal flags:
#
# - We've previously caught 2 bugs causeed by an implicit cast
# to a smaller-width type: carefully inspect warnings reported
# by the '-Wconversion' flag.
#
# - We may like to warn about aggregate returns cause we don't
# want to explode the stack if the structure type returned got
# _innocently_ bigger over time. Check '-Waggregate-return'.
#
# Options are printed in GCC HTML documentation order.
#
CWARN_FLAGS =				\
  -Wall					\
  -Wextra				\
  -Wchar-subscripts			\
  -Wformat=2				\
  -Wmissing-include-dirs		\
  -Wparentheses				\
  -Wtrigraphs				\
  -Wunused				\
  -Wstrict-aliasing=2			\
  -Wundef				\
  -Wpointer-arith			\
  -Wcast-qual				\
  -Wwrite-strings			\
  -Waddress				\
  -Wlogical-op				\
  -Wstrict-prototypes			\
  -Wmissing-prototypes			\
  -Wmissing-declarations		\
  -Wmissing-noreturn			\
  -Wnormalized=nfc			\
  -Wredundant-decls			\
  -Wvla					\
  -Wdisabled-optimization		\
  -Wno-type-limits			\
  -Wno-missing-field-initializers

CFLAGS =				\
  $(CMACH_FLAGS)			\
  $(CDIALECT_FLAGS)			\
  $(COPT_FLAGS)				\
  $(CWARN_FLAGS)

# Share headers between assembly, C, and LD files
CPPFLAGS = -D__KERNEL__
AFLAGS = -D__ASSEMBLY__

# Warn about the sloppy UNIX linkers practice of
# merging global common variables
LDFLAGS = --warn-common

# Our global kernel linker script, after being
# 'cpp' pre-processed from the *.ld source
PROCESSED_LD_SCRIPT = kern/kernel.ldp

# GCC-generated C code header files dependencies
# Check '-MM' and '-MT' at gcc(1)
DEPS_ROOT_DIR = .deps
DEPS_DIRS    += $(DEPS_ROOT_DIR)

# 'Sparse' compiler wrapper
CGCC	= cgcc

#
# Object files listings
#

# Core and Secondary CPUs bootstrap
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/boot
BOOT_OBJS =		\
  boot/head.o		\
  boot/trampoline.o	\
  boot/rmcall.o		\
  boot/e820.o		\
  boot/load_ramdisk.o

# Memory management
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/mm
MM_OBJS =		\
  mm/e820.o		\
  mm/page_alloc.o	\
  mm/vm_map.o		\
  mm/kmalloc.o

# Devices
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/dev
DEV_OBJS =		\
  dev/serial.o		\
  dev/pic.o		\
  dev/apic.o		\
  dev/ioapic.o		\
  dev/pit.o		\
  dev/keyboard.o

# Ext2 file system
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/ext2
EXT2_OBJS =		\
  ext2/ext2.o		\
  ext2/ext2_debug.o	\
  ext2/file.o		\
  ext2/file_tests.o	\
  ext2/files_list.o

# Isolated library code
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/lib
LIB_OBJS =		\
  lib/list.o		\
  lib/unrolled_list.o	\
  lib/bitmap.o		\
  lib/string.o		\
  lib/printf.o		\
  lib/atomic.o		\
  lib/spinlock.o

# All other kernel objects
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/kern
KERN_OBJS =		\
  $(BOOT_OBJS)		\
  $(MM_OBJS)		\
  $(DEV_OBJS)		\
  $(EXT2_OBJS)		\
  $(LIB_OBJS)		\
  kern/idt.o		\
  kern/mptables.o	\
  kern/smpboot.o	\
  kern/sched.o		\
  kern/kthread.o	\
  kern/panic.o		\
  kern/percpu.o		\
  kern/ramdisk.o	\
  kern/main.o

BOOTSECT_OBJS =		\
  boot/bootsect.o

BUILD_OBJS    =		$(BOOTSECT_OBJS) $(KERN_OBJS)
BUILD_DIRS    =		$(DEPS_DIRS)

# Control output verbosity
# `@': suppresses echoing of subsequent command
VERBOSE=0
ifeq ($(VERBOSE), 0)
	E = @echo
	Q = @
else
	E = @\#
	Q =
endif

all: final

# Check kernel code against common semantic C errors
# using the 'sparse' semantic parser
#
# REAL_CC: do not depend on the system-wide CC envi-
# ronment variable, use our chosen compiler instead.
.PHONY: check
check: clean
	$(E) "  SPARSE build"
	$(Q) $(MAKE) REAL_CC=$(CC) CC=$(CGCC) all

#
# Build final, self-contained, bootable disk image
#

BOOT_BIN       = kern/image
RAMDISK_BIN    = build/ramdisk
FINAL_HD_IMAGE = build/hd-image
BUILD_SCRIPT   = tools/build-hdimage.py

final: $(BUILD_DIRS) $(FINAL_HD_IMAGE)
	$(E) "Disk image ready:" $(FINAL_HD_IMAGE)

$(FINAL_HD_IMAGE): $(BOOT_BIN) $(RAMDISK_BIN) $(BUILD_SCRIPT)
	$(E) "  PYTHON3  " $@
	$(Q) python $(BUILD_SCRIPT)

# If no ramdisk image exist, create an empty placeholder.
# Also create the 'build/' directory if it doesn't exist.
$(RAMDISK_BIN):
	$(Q) test -d 'build' || (echo "  MKDIR     build")
	$(Q) mkdir -p build
	$(E) "  TOUCH    " $@
	$(Q) touch $@

#
# Build kernel + bootsector ELF and binaries
#

BOOTSECT_ELF   = boot/bootsect.elf
BOOTSECT_BIN   = boot/bootsect.bin
KERNEL_ELF     = kern/kernel.elf
KERNEL_BIN     = kern/kernel.bin

$(BOOT_BIN): $(BOOTSECT_ELF) $(KERNEL_ELF)
	$(E) "  OBJCOPY  " $@
	$(Q) objcopy -O binary $(BOOTSECT_ELF) $(BOOTSECT_BIN)
	$(Q) objcopy -O binary $(KERNEL_ELF) $(KERNEL_BIN)
	$(Q) cat $(BOOTSECT_BIN) $(KERNEL_BIN) > $@

$(BOOTSECT_ELF): $(BOOTSECT_OBJS)
	$(E) "  LD       " $@
	$(Q) $(LD) $(LDFLAGS) -Ttext 0x0  $< -o $@

$(KERNEL_ELF): $(KERN_OBJS) $(PROCESSED_LD_SCRIPT)
	$(E) "  LD       " $@
	$(Q) $(LD) $(LDFLAGS) -T $(PROCESSED_LD_SCRIPT) $(KERN_OBJS) -o $@

# Patterns for custom implicit rules
%.o: %.S
	$(E) "  AS       " $@
	$(Q) $(CC) -c $(AFLAGS) $(CFLAGS) $< -o $@
	$(Q) $(CC) -MM $(AFLAGS) $(CFLAGS) $< -o $(DEPS_ROOT_DIR)/$*.d -MT $@
%.o: %.c
	$(E) "  CC       " $@
	$(Q) $(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@
	$(Q) $(CC) -MM $(CPPFLAGS) $(CFLAGS) $< -o $(DEPS_ROOT_DIR)/$*.d -MT $@
%.ldp: %.ld
	$(E) "  CPP      " $@
	$(Q) $(CPP) $(AFLAGS) $(CFLAGS) -P $< -O $@
	$(Q) $(CPP) -MM $(AFLAGS) $(CFLAGS) $< -o $(DEPS_ROOT_DIR)/$*.d -MT $@

# Needed build directories
$(BUILD_DIRS):
	$(E) "  MKDIR    " $@
	$(Q) mkdir -p $@

.PHONY: clean
clean:
	$(E) "  CLEAN"
	$(Q) rm -fr $(BUILD_DIRS)
	$(Q) rm -f  $(BUILD_OBJS)
	$(Q) rm -f  $(PROCESSED_LD_SCRIPT)
	$(Q) rm -f  $(BOOTSECT_ELF) $(BOOTSECT_BIN)
	$(Q) rm -f  $(KERNEL_ELF) $(KERNEL_BIN)
	$(Q) rm -f  $(BOOT_BIN) $(FINAL_HD_IMAGE)

# Include generated dependency files
# `-': no error, not even a warning, if any of the given
# filenames do not exist
-include $(DEPS_ROOT_DIR)/*.d
-include $(DEPS_ROOT_DIR)/*/*.d
