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
PROCESSED_LD_SCRIPT = kernel.ldp

LIB_OBJS = lib/string.o lib/printf.o

# Bootsector object won't be linked with the kernel;
# handle it differently
KERN_OBJS = head.o e820.o common.o main.o idt.o i8259.o \
            apic.o ioapic.o mptables.o keyboard.o smpboot.o \
            pit.o trampoline.o spinlock.o page_alloc.o  \
            kmalloc.o memory_map.o $(LIB_OBJS)
OBJS = bootsect.o $(KERN_OBJS)

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

# GCC-generated object files dependencies folders
# See `-MM' and `-MT' at gcc(1)
DEPS_DIR = .deps
DEPS_LIB = $(DEPS_DIR)/lib
BUILD_DIRS = $(DEPS_DIR) $(DEPS_LIB)

all: $(BUILD_DIRS) image
	$(E) "Kernel ready"

# Check kernel code against common semantic C errors
# using the 'sparse' semantic parser
.PHONY: check
check: clean
	$(E) "  SPARSE build"
	$(Q) $(MAKE) CC=$(CGCC) all

image: bootsect.elf kernel.elf
	$(E) "  OBJCOPY  " $@
	$(Q) objcopy -O binary bootsect.elf bootsect.bin
	$(Q) objcopy -O binary kernel.elf kernel.bin
	$(Q) cat bootsect.bin kernel.bin > $@

bootsect.elf: bootsect.o
	$(E) "  LD       " $@
	$(Q) $(LD) $(LDFLAGS) -Ttext 0x0  $< -o $@

kernel.elf: $(KERN_OBJS) $(PROCESSED_LD_SCRIPT)
	$(E) "  LD       " $@
	$(Q) $(LD) $(LDFLAGS) -T $(PROCESSED_LD_SCRIPT) $(KERN_OBJS) -o $@

# Patterns for custom implicit rules
%.o: %.S
	$(E) "  AS       " $@
	$(Q) $(CC) -c $(AFLAGS) $(CFLAGS) $< -o $@
	$(Q) $(CC) -MM $(AFLAGS) $(CFLAGS) $< -o $(DEPS_DIR)/$*.d -MT $@
%.o: %.c
	$(E) "  CC       " $@
	$(Q) $(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@
	$(Q) $(CC) -MM $(CPPFLAGS) $(CFLAGS) $< -o $(DEPS_DIR)/$*.d -MT $@
%.ldp: %.ld
	$(E) "  CPP      " $@
	$(Q) $(CPP) $(AFLAGS) $(CFLAGS) -P $< -O $@
	$(Q) $(CPP) -MM $(AFLAGS) $(CFLAGS) $< -o $(DEPS_DIR)/$*.d -MT $@

# Needed build directories
$(BUILD_DIRS):
	$(E) "  MKDIR    " $@
	$(Q) mkdir -p $@

.PHONY: clean
clean:
	$(E) "  CLEAN"
	$(Q) rm -f image
	$(Q) rm -f $(OBJS)
	$(Q) rm -f *.bin
	$(Q) rm -f *.elf
	$(Q) rm -f $(PROCESSED_LD_SCRIPT)
	$(Q) rm -fr $(BUILD_DIRS)
	$(Q) rm -f *~

# Include generated dependency files
# `-': no error, not even a warning, if any of the given
# filenames do not exist
-include $(DEPS_DIR)/*.d
-include $(DEPS_LIB)/*.d
