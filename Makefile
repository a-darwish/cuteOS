#
# Cute Makefile
#

CC	= gcc
LD	= ld

# After using -nostdinc, add compiler's specific
# includes back (stdarg.h, etc) using -iwithprefix
CFLAGS  = -m64 --std=gnu99 -mcmodel=kernel \
	  -fno-builtin -nostdlib \
	  -nostdinc -iwithprefix include -I include \
	  -Wall -Wstrict-prototypes -O2

# Share headers between assembly and C files
CPPFLAGS = -D__KERNEL__
AFLAGS = -D__ASSEMBLY__

LIB_OBJS = lib/string.o lib/printf.o

# Bootsector object won't be linked with the kernel;
# handle it differently
KERN_OBJS = head.o common.o main.o idt.o i8259.o apic.o \
            ioapic.o mptables.o keyboard.o smpboot.o \
            pit.o trampoline.o $(LIB_OBJS)
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

image: bootsect.elf kernel.elf
	$(E) "  OBJCOPY  " $@
	$(Q) objcopy -O binary bootsect.elf bootsect.bin
	$(Q) objcopy -O binary kernel.elf kernel.bin
	$(Q) cat bootsect.bin kernel.bin > $@

bootsect.elf: bootsect.o
	$(E) "  LD       " $@
	$(Q) $(LD) -Ttext 0x0  $< -o $@

kernel.elf: $(KERN_OBJS) kernel.ld
	$(E) "  LD       " $@
	$(Q) $(LD) -T kernel.ld $(KERN_OBJS) -o $@

# Patterns for custom implicit rules
%.o: %.S
	$(E) "  AS       " $@
	$(Q) $(CC) -c $(AFLAGS) $(CFLAGS) $< -o $@
	$(Q) $(CC) -MM $(AFLAGS) $(CFLAGS) $< -o $(DEPS_DIR)/$*.d -MT $@
%.o: %.c
	$(E) "  CC       " $@
	$(Q) $(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@
	$(Q) $(CC) -MM $(CPPFLAGS) $(CFLAGS) $< -o $(DEPS_DIR)/$*.d -MT $@

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
	$(Q) rm -fr $(BUILD_DIRS)
	$(Q) rm -f *~

# Include generated dependency files
# `-': no error, not even a warning, if any of the given
# filenames do not exist
-include $(DEPS_DIR)/*.d
-include $(DEPS_LIB)/*.d
