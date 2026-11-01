# Casiolator Loopy sound test cartridge. Needs sh-elf-gcc (on Windows: in WSL,
#   wsl make -C tests/loopy_soundtest
# ). The result, soundtest.bin, is a big-endian Loopy ROM image.

CC = sh-elf-gcc
OBJCOPY = sh-elf-objcopy
CFLAGS = -m1 -mb -Os -ffreestanding -fno-builtin -Wall

soundtest.bin: soundtest.elf
	$(OBJCOPY) -O binary $< $@
	python3 checksum.py $@

soundtest.elf: start.s main.c link.ld
	$(CC) $(CFLAGS) -nostdlib -Wl,--no-warn-rwx-segments -T link.ld start.s main.c -lgcc -o $@

clean:
	rm -f soundtest.elf soundtest.bin
