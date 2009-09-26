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
	  -Wall -Wstrict-prototypes

# Share headers between assembly and C files
CPPFLAGS = -D__KERNEL__
AFLAGS = -D__ASSEMBLY__

# Bootsector object won't be linked with the kernel;
# handle it differently
KERN_OBJS = head.o main.o printf.o string.o idt.o
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

# GCC-generated object files dependencies folder
# See `-MM' and `-MT' at gcc(1)
DEPS_DIR = .deps
BUILD_DIRS = $(DEPS_DIR)

all: $(BUILD_DIRS) image
	$(E) "Kernel ready"

image: bootsect.elf kernel.elf
	$(E) "  OBJCOPY  " $@
	$(Q) objcopy -O binary bootsect.elf bootsect.bin
	$(Q) objcopy -O binary --only-section '.iarea' kernel.elf head.bin
	$(Q) objcopy -O binary --only-section '.varea' kernel.elf kernel.bin
	$(Q) cat bootsect.bin head.bin kernel.bin > $@

bootsect.elf: bootsect.o
	$(E) "  LD       " $@
	$(Q) $(LD) -Ttext 0x0  $< -o $@

kernel.elf: $(KERN_OBJS)
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
	$(Q) mkdir $@

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
