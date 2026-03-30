set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

build_dir := "build"
iso_dir := "iso"
cflags := "-m32 -march=i686 -O2 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostartfiles"
inc_kernel := "-Iinclude -Imm"
inc_user := "-Iinclude -Iuser"

default: build

build:
    mkdir -p {{build_dir}}
    python3 tools/generate_icons.py icons {{build_dir}}/icon_assets.c
    python3 tools/generate_cursors.py cursors {{build_dir}}/cursor_assets.c
    gcc {{cflags}} {{inc_kernel}} -c src/boot/boot.s -o {{build_dir}}/boot.o
    gcc {{cflags}} {{inc_kernel}} -c src/kernel/kernel.c -o {{build_dir}}/kernel.o
    gcc {{cflags}} {{inc_kernel}} -c src/kernel/userland.c -o {{build_dir}}/userland.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/gdt.c -o {{build_dir}}/gdt.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/idt.c -o {{build_dir}}/idt.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/gfx.c -o {{build_dir}}/gfx.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/keyboard.c -o {{build_dir}}/keyboard.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/mouse.c -o {{build_dir}}/mouse.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/pit.c -o {{build_dir}}/pit.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/speaker.c -o {{build_dir}}/speaker.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/dma.c -o {{build_dir}}/dma.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/sb16.c -o {{build_dir}}/sb16.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/serial.c -o {{build_dir}}/serial.o
    gcc {{cflags}} {{inc_kernel}} -c audio/audio.c -o {{build_dir}}/audio.o
    gcc {{cflags}} {{inc_kernel}} -c audio/synth.c -o {{build_dir}}/synth.o
    gcc {{cflags}} {{inc_kernel}} -c {{build_dir}}/icon_assets.c -o {{build_dir}}/icon_assets.o
    gcc {{cflags}} {{inc_kernel}} -c {{build_dir}}/cursor_assets.c -o {{build_dir}}/cursor_assets.o
    gcc {{cflags}} {{inc_kernel}} -c desktop/desktop.c -o {{build_dir}}/desktop.o
    for src in apps/*.c; do gcc {{cflags}} {{inc_kernel}} -c "$src" -o {{build_dir}}/"$(basename "${src%.c}")".o; done
    gcc {{cflags}} {{inc_kernel}} -c kshell.c -o {{build_dir}}/kshell.o
    gcc {{cflags}} {{inc_kernel}} -c mem/pmm.c -o {{build_dir}}/pmm.o
    gcc {{cflags}} {{inc_kernel}} -c mem/vmm.c -o {{build_dir}}/vmm.o
    gcc {{cflags}} {{inc_kernel}} -c mm/paging.c -o {{build_dir}}/paging.o
    gcc {{cflags}} {{inc_kernel}} -c mm/vm.c -o {{build_dir}}/vm.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/interrupt.s -o {{build_dir}}/interrupt.o
    ld -m elf_i386 -T linker/linker.ld -o {{build_dir}}/coffeeos.elf {{build_dir}}/boot.o {{build_dir}}/kernel.o {{build_dir}}/userland.o {{build_dir}}/gdt.o {{build_dir}}/idt.o {{build_dir}}/gfx.o {{build_dir}}/keyboard.o {{build_dir}}/mouse.o {{build_dir}}/pit.o {{build_dir}}/speaker.o {{build_dir}}/dma.o {{build_dir}}/sb16.o {{build_dir}}/serial.o {{build_dir}}/audio.o {{build_dir}}/synth.o {{build_dir}}/icon_assets.o {{build_dir}}/cursor_assets.o {{build_dir}}/desktop.o $(for src in apps/*.c; do printf '%s/%s.o ' {{build_dir}} "$(basename "${src%.c}")"; done) {{build_dir}}/kshell.o {{build_dir}}/pmm.o {{build_dir}}/vmm.o {{build_dir}}/paging.o {{build_dir}}/vm.o {{build_dir}}/interrupt.o

user:
    mkdir -p {{build_dir}}
    gcc {{cflags}} {{inc_user}} -c user/crt0.s -o {{build_dir}}/user_crt0.o
    gcc {{cflags}} {{inc_user}} -c user/shell.c -o {{build_dir}}/user_shell.o
    ld -m elf_i386 -T user/user.ld -o {{build_dir}}/user.elf {{build_dir}}/user_crt0.o {{build_dir}}/user_shell.o

iso: build user
    mkdir -p {{iso_dir}}/boot/grub
    cp {{build_dir}}/coffeeos.elf {{iso_dir}}/boot/coffeeos.elf
    cp {{build_dir}}/user.elf {{iso_dir}}/boot/user.elf
    cp grub/grub.cfg {{iso_dir}}/boot/grub/grub.cfg
    grub-mkrescue -o {{build_dir}}/coffeeos.iso {{iso_dir}}

run: iso
    # prefer modern -device/-audiodev; older QEMU can still fall back to -soundhw sb16
    qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -serial stdio -audiodev sdl,id=snd0 -device sb16,audiodev=snd0 || qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -serial stdio -soundhw sb16

run-headless: iso
    qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -serial stdio -display none -audiodev sdl,id=snd0 -device sb16,audiodev=snd0 || qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -serial stdio -display none -soundhw sb16

clean:
    rm -rf {{build_dir}} {{iso_dir}}
