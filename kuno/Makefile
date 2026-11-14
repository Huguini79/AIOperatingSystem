CC      = gcc
CFLAGS  = -m64 -O2 -Wall -Wextra -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -mno-red-zone -mgeneral-regs-only -fno-builtin -nostdlib -Iinclude
SRC     = $(wildcard kernel/*.c)
OBJ     = build/boot.o $(SRC:kernel/%.c=build/%.o)

all: build/kuno.elf tools/kcc

build/boot.o: boot/boot.S | build
	$(CC) -m64 -c $< -o $@
build/%.o: kernel/%.c include/kernel.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/kuno.elf: $(OBJ) link.ld
	ld -n -z max-page-size=0x1000 -T link.ld -o $@ $(OBJ)
build:
	mkdir -p build

# --- compilador propio (se ejecuta en el host por ahora) ---
tools/kcc: tools/kcc.c
	$(CC) -O2 -Wall -o $@ $<

# --- pruebas en el host ---
HOSTCC = $(CC) -O1 -Wall -Wextra -Iinclude
test: tools/kcc | build
	python3 tools/mkfat32.py build/disk.img HELLO.TXT="Hola desde FAT32!" "Archivo con nombre largo.txt=LFN ok" sub/inner.txt=hello big.bin=@5000
	$(HOSTCC) -o build/fat32_test tests/fat32_test.c kernel/fat32.c && ./build/fat32_test build/disk.img
	$(HOSTCC) -o build/input_test tests/input_test.c kernel/input.c tests/stubs.c && ./build/input_test
	$(HOSTCC) -o build/shell_test tests/shell_test.c kernel/shell.c kernel/fat32.c tests/stubs.c && ./build/shell_test build/disk.img
	./tools/kcc tests/prog.kc > build/prog.s
	$(CC) -o build/prog build/prog.s tests/runtime.c && ./build/prog | tr '\n' ' '; echo
# captura de la GUI real (build/snap1.png, build/snap2.png) para revisar el aspecto sin QEMU
snap: test
	$(HOSTCC) -o build/gui_snapshot tests/gui_snapshot.c kernel/gui.c kernel/font.c kernel/shell.c kernel/fat32.c kernel/input.c tests/stubs.c
	./build/gui_snapshot build/disk.img
	python3 tools/ppm2png.py build/snap1.ppm build/snap1.png && python3 tools/ppm2png.py build/snap2.ppm build/snap2.png

# --- ISO arrancable en BIOS y UEFI (requiere grub-mkrescue + xorriso) ---
iso: build/kuno.elf
	mkdir -p build/iso/boot/grub
	cp build/kuno.elf build/iso/boot/ && cp build/disk.img build/iso/boot/ # ejecuta 'make test' antes para crear disk.img
	printf 'set timeout=0\nmenuentry "Kuno" {\n multiboot2 /boot/kuno.elf\n module2 /boot/disk.img\n}\n' > build/iso/boot/grub/grub.cfg
	grub-mkrescue -o build/kuno.iso build/iso
run-bios: iso
	qemu-system-x86_64 -m 256 -cdrom build/kuno.iso -serial stdio -vga std
run-uefi: iso
	qemu-system-x86_64 -m 256 -bios /usr/share/ovmf/OVMF.fd -cdrom build/kuno.iso -serial stdio -vga std
run-2gpu: iso
	qemu-system-x86_64 -m 256 -cdrom build/kuno.iso -serial stdio -vga std -device secondary-vga
clean:
	rm -rf build tools/kcc
