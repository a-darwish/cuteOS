
AS	= as
CC	= gcc
CPP	= $(CC) -E
LD	= ld

# After using -nostdinc, add compiler's specific
# includes back (stdarg.h, etc) using -iwithprefix
CFLAGS  = -m64 --std=gnu99 -fno-builtin -nostdinc -nostdlib \
	  -iwithprefix include

all: bootsect.bin kernel.bin
	cat bootsect.bin kernel.bin > image

# See comment on tope of bootsector's _start
bootsect.bin: bootsect.o
	$(LD) -Ttext 0x0 -s --oformat binary $< -o $@
bootsect.o: bootsect.S
	$(CPP) $(CFLAGS) -traditional $< -o bootsect.s
	$(AS) bootsect.s -o $@
	rm bootsect.s

# Current bootsector load the kernel at 0x100000
# We may use --print-map for a final kernel LMA map
kernel.bin: head_asm.o head_c.o printf.o
	$(LD) -T kernel.ld $^ -o $@
head_asm.o: head.S
	$(CPP) $(CFLAGS) -traditional $< -o head.s
	$(AS) head.s -o $@
	rm head.s
head_c.o: head.c
	$(CC) $(CFLAGS) -c $< -o $@
printf.o: printf.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f image
	rm -f *.s
	rm -f *.o
	rm -f *.bin
	rm -f *~
