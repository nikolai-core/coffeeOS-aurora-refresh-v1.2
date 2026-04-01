# coffeeOS Aurora refresh v1.3

coffeeOS Aurora refresh v1.3 is a 32-bit x86 hobby operating system kernel built in C and x86 assembly. It boots through Multiboot into a higher-half kernel, brings up paging, interrupts, framebuffer graphics, keyboard, mouse, PIT timing, serial logging, audio, a desktop UI, a kernel shell, userland syscall plumbing, and a FAT32-backed filesystem.

## Highlights

- Higher-half 32-bit x86 kernel with GDT, TSS, IDT, paging, and PMM/VMM layers
- Framebuffer desktop with movable windows, icons, terminal, and app launcher
- Built-in apps including Notepad, Paint, Calculator, Clock, Mixer, System Info, and Files
- FAT32 filesystem with VFS layer, sector cache, shell commands, syscalls, ramdisk boot image support, and ATA disk support
- Persistent storage workflow through an external raw disk image
- Windows-friendly export/import path via fixed VHD conversion
- SB16 audio path with PC speaker fallback

## Repository layout

- `src/boot/boot.s`: Multiboot entry and early bootstrap
- `src/kernel/kernel.c`: kernel bring-up and boot device selection
- `cpu/`: descriptor tables, interrupt glue, and syscall dispatch
- `drivers/`: hardware-facing drivers including graphics, input, timer, serial, ATA, and ramdisk
- `mem/` and `mm/`: physical and virtual memory management
- `fs/`: block devices, MBR parsing, FAT32, formatter, and VFS
- `desktop/` and `apps/`: windowing, desktop shell, and GUI apps
- `tools/`: host-side asset and disk-image tooling

## Requirements

- `gcc` with 32-bit output support
- `ld`
- `grub-mkrescue`
- `qemu-system-i386`
- `python3`
- `just`

## Build and run

Rebuild everything from a clean tree:

```bash
just rebuild
```

Persistent run with ATA storage and RTL8139 networking:

```bash
just run-persist
```

Persistent run with ATA storage, RTL8139 networking, and SB16 audio:

```bash
just run-persist-audio
```

Remove generated build outputs:

```bash
just clean
```

## Persistence workflow

coffeeOS uses two disk paths:

- `build/disk.img`: raw FAT32 image embedded into the ISO as a Multiboot module used at boot
- `build/persist.img`: raw MBR + FAT32 disk image attached as an ATA disk for persistent sessions

Inside coffeeOS, use `sync` before closing QEMU, or `reboot`, to flush FAT32 metadata and cached writes.

Create a new persistent disk:

```bash
just mkdisk
```

Boot the persistent disk with networking:

```bash
just run-persist
```

Boot the persistent disk with networking and audio:

```bash
just run-persist-audio
```

## Windows VHD conversion

The runtime persistent disk is `build/persist.img`. When you want to inspect or edit it from Windows Disk Management, convert it to a fixed VHD:

```bash
just img-to-vhd
```

This writes `build/persist.vhd`.

After editing the VHD in Windows, convert it back for coffeeOS:

```bash
just vhd-to-img
```

This restores `build/persist.img`.

Do not mount the VHD in Windows while QEMU is running.

## Shell and filesystem

The kernel shell and GUI terminal share the same dispatcher. Filesystem commands include:

- `ls`
- `cat`
- `write`
- `mkdir`
- `rm`
- `rmdir`
- `cp`
- `mv`
- `stat`
- `df`
- `touch`
- `edit`
- `sync`

Notepad supports:

- `Ctrl+S` save
- `Ctrl+N` new document
- `Ctrl+O` open by path

The Files app opens `.txt` files directly in Notepad.

## Notes

- `just run-persist` is the default workflow for saved files across QEMU sessions
- `just run-persist-audio` adds SB16 audio on top of the same persistent networked setup
- Both persistent run targets attach the RTL8139 NIC with QEMU user networking
- The project is freestanding: `-ffreestanding`, `-nostdlib`
- Existing packed-member warnings in FAT32 LFN decoding are known and currently tolerated
