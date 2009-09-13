AS	= as
CC	= gcc
CPP	= $(CC) -E -nostdinc
LD	= ld

all: bootsect.bin kernel.bin
	cat bootsect.bin kernel.bin > image

# see comment on tope of bootsector's _start
bootsect.bin: bootsect.o
	$(LD) -Ttext 0x0 -s --oformat binary $< -o $@
bootsect.o: bootsect.S
	$(CPP) -traditional $< -o bootsect.s
	$(AS) bootsect.s -o $@
	rm bootsect.s

# current bootsector load the kernel at 0x100000
kernel.bin: kernel.o
	$(LD) -Ttext 0x100000 -s --oformat binary $< -o $@
kernel.o: head.S
	$(CPP) -traditional $< -o kernel.s
	$(AS) kernel.s -o $@
	rm kernel.s

clean:
	rm -f *.s
	rm -f *.o
	rm -f *.bin
	rm -f *~
