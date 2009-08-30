AS	= as
CC	= gcc
CPP	= $(CC) -E -nostdinc
LD	= ld

# see comment on tope of bootsector's _start
bootsect.bin: bootsect.o
	$(LD) -Ttext 0x0 -s --oformat binary $< -o $@
bootsect.o: bootsect.S
	$(CPP) -traditional $< -o bootsect.s
	$(AS) bootsect.s -o $@
	rm bootsect.s

clean:
	rm -f *.s
	rm -f *.o
	rm -f *.bin
	rm -f *~
