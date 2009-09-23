
AS	= as
CC	= gcc
CPP	= $(CC) -E
LD	= ld

# After using -nostdinc, add compiler's specific
# includes back (stdarg.h, etc) using -iwithprefix
CFLAGS  = -m64 --std=gnu99 -mcmodel=kernel \
	  -fno-builtin -nostdlib \
	  -nostdinc -iwithprefix include -I include \
	  -Wall -Wstrict-prototypes

CPPFLAGS = $(CFLAGS) -D__ASSEMBLY__

all: bootsect.bin kernel.elf
	objcopy -O binary --only-section '.iarea' kernel.elf head.bin
	objcopy -O binary --only-section '.varea' kernel.elf kernel.bin
	cat bootsect.bin head.bin kernel.bin > image

# See comment on tope of bootsector's _start
bootsect.bin: bootsect.o
	$(LD) -Ttext 0x0 -s --oformat binary $< -o $@
bootsect.o: bootsect.S
	$(CPP) $(CPPFLAGS) -traditional $< -o bootsect.s
	$(AS) bootsect.s -o $@
	rm bootsect.s

# Current bootsector load the kernel at 0x100000
# We may use --print-map for a final kernel LMA map
kernel.elf: head.o head_c.o printf.o string.o idt.o
	$(LD) -T kernel.ld $^ -o $@
head.o: head.S
	$(CC) $(CPPFLAGS) -c $< -o $@
head_c.o: head.c
	$(CC) $(CFLAGS) -c $< -o $@
printf.o: printf.c
	$(CC) $(CFLAGS) -c $< -o $@
string.o: string.c
	$(CC) $(CFLAGS) -c $< -o $@
idt.o: idt.S
	$(CC) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f image
	rm -f *.s
	rm -f *.o
	rm -f *.bin
	rm -f *.elf
	rm -f *~
