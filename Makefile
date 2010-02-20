#
# Cute Makefile
#

CC	= gcc
CPP	= cpp
LD	= ld

# 'Sparse' compiler wrapper
CGCC	= cgcc

# After using -nostdinc, add compiler's specific
# includes back (stdarg.h, etc) using -iwithprefix
CFLAGS  = -m64 --std=gnu99 -mcmodel=kernel \
	  -fno-builtin -nostdlib \
	  -nostdinc -iwithprefix include -I include \
	  -Wall -Wstrict-prototypes -O2

# Warn about the sloppy UNIX linkers practice of
# merging global common variables
LDFLAGS = --warn-common

# Share headers between assembly and C files
CPPFLAGS = -D__KERNEL__
AFLAGS = -D__ASSEMBLY__

# Our global kernel linker script, after being
# 'cpp' pre-processed from the *.ld source
PROCESSED_LD_SCRIPT = kern/kernel.ldp

# GCC-generated C code header files dependencies
# Check '-MM' and '-MT' at gcc(1)
DEPS_ROOT_DIR = .deps
DEPS_DIRS    += $(DEPS_ROOT_DIR)

#
# Object files listings
#

# Core and Secondary CPUs bootstrap
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/boot
BOOT_OBJS =		\
  boot/head.o		\
  boot/e820.o		\
  boot/trampoline.o

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
  dev/i8259.o		\
  dev/apic.o		\
  dev/ioapic.o		\
  dev/pit.o		\
  dev/keyboard.o

# Isolated library code
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/lib
LIB_OBJS =		\
  lib/string.o		\
  lib/printf.o		\
  lib/spinlock.o

# All other kernel objects
DEPS_DIRS		+= $(DEPS_ROOT_DIR)/kern
KERN_OBJS =		\
  $(BOOT_OBJS)		\
  $(MM_OBJS)		\
  $(DEV_OBJS)		\
  $(LIB_OBJS)		\
  kern/idt.o		\
  kern/mptables.o	\
  kern/smpboot.o	\
  kern/common.o		\
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

all: $(BUILD_DIRS) image
	$(E) "Kernel ready"

# Check kernel code against common semantic C errors
# using the 'sparse' semantic parser
.PHONY: check
check: clean
	$(E) "  SPARSE build"
	$(Q) $(MAKE) CC=$(CGCC) all

#
# Build final, self-contained, bootable image
#

BOOTSECT_ELF = boot/bootsect.elf
BOOTSECT_BIN = boot/bootsect.bin

KERNEL_ELF   = kern/kernel.elf
KERNEL_BIN   = kern/kernel.bin

image: $(BOOTSECT_ELF) $(KERNEL_ELF)
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
	$(Q) rm -f  image
	$(Q) rm -f  $(BUILD_OBJS)
	$(Q) rm -fr $(BUILD_DIRS)
	$(Q) rm -f  $(PROCESSED_LD_SCRIPT)
	$(Q) rm -f  $(BOOTSECT_ELF) $(BOOTSECT_BIN)
	$(Q) rm -f  $(KERNEL_ELF) $(KERNEL_BIN)

# Include generated dependency files
# `-': no error, not even a warning, if any of the given
# filenames do not exist
-include $(DEPS_ROOT_DIR)/*.d
-include $(DEPS_ROOT_DIR)/*/*.d
