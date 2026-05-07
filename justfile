set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# coffeeOS aurora refresh build system
# ─────────────────────────────────────
# just rebuild          — clean build outputs and rebuild the ISO
# just run-persist      — persistent run with networking
# just run-persist-audio — persistent run with networking and SB16 audio
# just build-only       — rebuild kernel without touching persistent disks

build_dir := "build"
iso_dir := "iso"
cflags := "-m32 -march=i686 -O2 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -nostartfiles"
inc_kernel := "-Iinclude -Imm -Imem"
inc_user := "-Iinclude -Iuser"
qemu_net := "-nic user,model=rtl8139"

default: build

build:
    mkdir -p {{build_dir}}
    python3 tools/generate_icons.py icons {{build_dir}}/icon_assets.c
    python3 tools/generate_cursors.py cursors {{build_dir}}/cursor_assets.c
    python3 tools/generate_boot_logo.py assets/boot_logo.jpg {{build_dir}}/boot_logo_asset.c
    gcc {{cflags}} {{inc_kernel}} -c src/boot/boot.s -o {{build_dir}}/boot.o
    gcc {{cflags}} {{inc_kernel}} -c src/kernel/kernel.c -o {{build_dir}}/kernel.o
    gcc {{cflags}} {{inc_kernel}} -c src/kernel/process.c -o {{build_dir}}/process.o
    gcc {{cflags}} {{inc_kernel}} -c src/kernel/userland.c -o {{build_dir}}/userland.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/gdt.c -o {{build_dir}}/gdt.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/idt.c -o {{build_dir}}/idt.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/panic.c -o {{build_dir}}/panic.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/gfx.c -o {{build_dir}}/gfx.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/keyboard.c -o {{build_dir}}/keyboard.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/mouse.c -o {{build_dir}}/mouse.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/pit.c -o {{build_dir}}/pit.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/speaker.c -o {{build_dir}}/speaker.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/dma.c -o {{build_dir}}/dma.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/pci.c -o {{build_dir}}/pci.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/rtl8139.c -o {{build_dir}}/rtl8139.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/sb16.c -o {{build_dir}}/sb16.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/serial.c -o {{build_dir}}/serial.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/ata.c -o {{build_dir}}/ata.o
    gcc {{cflags}} {{inc_kernel}} -c drivers/ramdisk.c -o {{build_dir}}/ramdisk.o
    gcc {{cflags}} {{inc_kernel}} -c net/netif.c -o {{build_dir}}/netif.o
    gcc {{cflags}} {{inc_kernel}} -c net/ethernet.c -o {{build_dir}}/ethernet.o
    gcc {{cflags}} {{inc_kernel}} -c net/arp.c -o {{build_dir}}/arp.o
    gcc {{cflags}} {{inc_kernel}} -c net/ip.c -o {{build_dir}}/ip.o
    gcc {{cflags}} {{inc_kernel}} -c net/icmp.c -o {{build_dir}}/icmp.o
    gcc {{cflags}} {{inc_kernel}} -c net/udp.c -o {{build_dir}}/udp.o
    gcc {{cflags}} {{inc_kernel}} -c net/dhcp.c -o {{build_dir}}/dhcp.o
    gcc {{cflags}} {{inc_kernel}} -c net/dns.c -o {{build_dir}}/dns.o
    gcc {{cflags}} {{inc_kernel}} -c net/tcp.c -o {{build_dir}}/tcp.o
    gcc {{cflags}} {{inc_kernel}} -c net/http.c -o {{build_dir}}/http.o
    gcc {{cflags}} {{inc_kernel}} -c net/net.c -o {{build_dir}}/net.o
    gcc {{cflags}} {{inc_kernel}} -c audio/audio.c -o {{build_dir}}/audio.o
    gcc {{cflags}} {{inc_kernel}} -c audio/synth.c -o {{build_dir}}/synth.o
    gcc {{cflags}} {{inc_kernel}} -c fs/blkdev.c -o {{build_dir}}/blkdev.o
    gcc {{cflags}} {{inc_kernel}} -c fs/mbr.c -o {{build_dir}}/mbr.o
    gcc {{cflags}} {{inc_kernel}} -c fs/fat32.c -o {{build_dir}}/fat32.o
    gcc {{cflags}} {{inc_kernel}} -c fs/fat32_format.c -o {{build_dir}}/fat32_format.o
    gcc {{cflags}} {{inc_kernel}} -c fs/vfs.c -o {{build_dir}}/vfs.o
    gcc {{cflags}} {{inc_kernel}} -c {{build_dir}}/icon_assets.c -o {{build_dir}}/icon_assets.o
    gcc {{cflags}} {{inc_kernel}} -c {{build_dir}}/cursor_assets.c -o {{build_dir}}/cursor_assets.o
    gcc {{cflags}} {{inc_kernel}} -c {{build_dir}}/boot_logo_asset.c -o {{build_dir}}/boot_logo_asset.o
    gcc {{cflags}} {{inc_kernel}} -c desktop/boot_animation.c -o {{build_dir}}/boot_animation.o
    gcc {{cflags}} {{inc_kernel}} -c desktop/desktop.c -o {{build_dir}}/desktop.o
    for src in apps/*.c; do gcc {{cflags}} {{inc_kernel}} -c "$src" -o {{build_dir}}/"$(basename "${src%.c}")".o; done
    gcc {{cflags}} {{inc_kernel}} -c kshell.c -o {{build_dir}}/kshell.o
    gcc {{cflags}} {{inc_kernel}} -c mem/pmm.c -o {{build_dir}}/pmm.o
    gcc {{cflags}} {{inc_kernel}} -c mem/vmm.c -o {{build_dir}}/vmm.o
    gcc {{cflags}} {{inc_kernel}} -c mm/paging.c -o {{build_dir}}/paging.o
    gcc {{cflags}} {{inc_kernel}} -c mm/vm.c -o {{build_dir}}/vm.o
    gcc {{cflags}} {{inc_kernel}} -c cpu/interrupt.s -o {{build_dir}}/interrupt.o
    ld -m elf_i386 -T linker/linker.ld -o {{build_dir}}/coffeeos.elf {{build_dir}}/boot.o {{build_dir}}/kernel.o {{build_dir}}/process.o {{build_dir}}/userland.o {{build_dir}}/gdt.o {{build_dir}}/idt.o {{build_dir}}/panic.o {{build_dir}}/gfx.o {{build_dir}}/keyboard.o {{build_dir}}/mouse.o {{build_dir}}/pit.o {{build_dir}}/speaker.o {{build_dir}}/dma.o {{build_dir}}/pci.o {{build_dir}}/rtl8139.o {{build_dir}}/sb16.o {{build_dir}}/serial.o {{build_dir}}/ata.o {{build_dir}}/ramdisk.o {{build_dir}}/netif.o {{build_dir}}/ethernet.o {{build_dir}}/arp.o {{build_dir}}/ip.o {{build_dir}}/icmp.o {{build_dir}}/udp.o {{build_dir}}/dhcp.o {{build_dir}}/dns.o {{build_dir}}/tcp.o {{build_dir}}/http.o {{build_dir}}/net.o {{build_dir}}/audio.o {{build_dir}}/synth.o {{build_dir}}/blkdev.o {{build_dir}}/mbr.o {{build_dir}}/fat32.o {{build_dir}}/fat32_format.o {{build_dir}}/vfs.o {{build_dir}}/icon_assets.o {{build_dir}}/cursor_assets.o {{build_dir}}/boot_logo_asset.o {{build_dir}}/boot_animation.o {{build_dir}}/desktop.o $(for src in apps/*.c; do printf '%s/%s.o ' {{build_dir}} "$(basename "${src%.c}")"; done) {{build_dir}}/kshell.o {{build_dir}}/pmm.o {{build_dir}}/vmm.o {{build_dir}}/paging.o {{build_dir}}/vm.o {{build_dir}}/interrupt.o

disk:
    python3 tools/make_disk.py {{build_dir}}/disk.img 16

mkdisk:
    python3 tools/make_disk.py {{build_dir}}/persist.img 16 --partitioned

img-to-vhd:
    python3 tools/convert_disk.py img-to-vhd {{build_dir}}/persist.img {{build_dir}}/persist.vhd

vhd-to-img:
    python3 tools/convert_disk.py vhd-to-img {{build_dir}}/persist.vhd {{build_dir}}/persist.img

user:
    mkdir -p {{build_dir}}
    gcc {{cflags}} {{inc_user}} -c user/crt0.s -o {{build_dir}}/user_crt0.o
    gcc {{cflags}} {{inc_user}} -c user/shell.c -o {{build_dir}}/user_shell.o
    ld -m elf_i386 -T user/user.ld -o {{build_dir}}/user.elf {{build_dir}}/user_crt0.o {{build_dir}}/user_shell.o

iso: build user disk
    mkdir -p {{iso_dir}}/boot/grub
    cp {{build_dir}}/coffeeos.elf {{iso_dir}}/boot/coffeeos.elf
    cp {{build_dir}}/user.elf {{iso_dir}}/boot/user.elf
    cp {{build_dir}}/disk.img {{iso_dir}}/boot/disk.img
    cp grub/grub.cfg {{iso_dir}}/boot/grub/grub.cfg
    grub-mkrescue -o {{build_dir}}/coffeeos.iso {{iso_dir}}

build-only: build user
    mkdir -p {{iso_dir}}/boot/grub
    if [ ! -f {{build_dir}}/disk.img ]; then python3 tools/make_disk.py {{build_dir}}/disk.img 16; fi
    cp {{build_dir}}/coffeeos.elf {{iso_dir}}/boot/coffeeos.elf
    cp {{build_dir}}/user.elf {{iso_dir}}/boot/user.elf
    cp {{build_dir}}/disk.img {{iso_dir}}/boot/disk.img
    cp grub/grub.cfg {{iso_dir}}/boot/grub/grub.cfg
    grub-mkrescue -o {{build_dir}}/coffeeos.iso {{iso_dir}}

run-persist: build-only
    if [ ! -f {{build_dir}}/persist.img ]; then python3 tools/make_disk.py {{build_dir}}/persist.img 16 --partitioned; fi
    qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -boot d -drive file={{build_dir}}/persist.img,format=raw,index=0,media=disk -serial stdio -m 256M -vga std -machine pc {{qemu_net}}

run-persist-audio: build-only
    if [ ! -f {{build_dir}}/persist.img ]; then python3 tools/make_disk.py {{build_dir}}/persist.img 16 --partitioned; fi
    # prefer modern -device/-audiodev; older QEMU can still fall back to -soundhw sb16
    qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -boot d -drive file={{build_dir}}/persist.img,format=raw,index=0,media=disk -serial stdio -m 256M -vga std -machine pc -audiodev sdl,id=snd0 -device sb16,audiodev=snd0 {{qemu_net}} || qemu-system-i386 -cdrom {{build_dir}}/coffeeos.iso -boot d -drive file={{build_dir}}/persist.img,format=raw,index=0,media=disk -serial stdio -m 256M -vga std -machine pc -soundhw sb16 {{qemu_net}}

clean:
    rm -rf {{build_dir}} {{iso_dir}}

rebuild: clean build-only
