# coffeeOS Aurora Refresh v1.4.0

coffeeOS Aurora Refresh v1.4.0 is a 32-bit x86 hobby operating system built in freestanding C and x86 assembly. It boots through Multiboot, initializes a higher-half kernel, brings up paging and interrupts, mounts FAT32 storage, and launches a custom desktop environment with a dirty-rectangle framebuffer compositor, PS/2 input, RTL8139 networking, SB16 audio, and built-in productivity apps.

> v1.4.0 is the current documentation refresh and maintenance release for the Aurora Refresh tree. Its most important user-visible fix is the desktop cursor ghost/imprint repair in the compositor path.

## At a glance

| Area | Current state |
| --- | --- |
| Kernel | 32-bit x86, higher-half, freestanding, Multiboot |
| Graphics | Backbuffer compositor with dirty-rect present and software cursor compositing |
| Desktop | Window manager, taskbar, icons, start menu, terminal, clock, system info |
| Storage | Block device layer, MBR parsing, FAT32, VFS, ramdisk and ATA workflows |
| Networking | RTL8139 path with Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP |
| Audio | SB16 output with PC speaker support paths |
| Apps | Hello App, Calculator, Files, Notepad, Paint, Audio Mixer, Network Monitor |

## Highlights

- Higher-half 32-bit x86 kernel with GDT, IDT, paging, PMM, and VMM layers
- Framebuffer desktop with movable windows, icons, built-in tools, and a GUI terminal
- Dirty-rectangle compositor with explicit cursor save, erase, redraw, and present sequencing
- FAT32 filesystem with VFS dispatch, ramdisk boot media support, and ATA-backed persistence
- RTL8139 networking stack with DHCP, DNS, TCP, UDP, and HTTP support
- SB16 audio path plus speaker support for simple fallback output
- App registry model that builds every `apps/*.c` translation unit automatically

## What changed in v1.4.0

### Cursor/compositor stability

The v1.4.0 maintenance pass fixes the desktop cursor imprint bug that could leave a permanent ghost on the screen after idle clock-driven redraws.

- Per-second dirty sources now update before the cursor erase gate in `desktop_run()`.
- `gfx_draw_cursor()` self-erases any already-composited cursor before saving replacement background pixels.
- The desktop loop now has an idle cursor-present fallback when the cursor has been erased but no other content required a full redraw.

Result: periodic clock and taskbar updates no longer burn cursor pixels into the desktop, and the cursor no longer disappears on an otherwise idle frame.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/boot/boot.s` | Multiboot entry and early bootstrap |
| `src/kernel/` | Kernel bring-up, top-level init, and userland plumbing |
| `cpu/` | Descriptor tables and interrupt glue |
| `drivers/` | Graphics, input, timer, serial, ATA, PCI, audio, and device drivers |
| `desktop/` | Desktop shell, compositor, taskbar, and window manager |
| `apps/` | Built-in GUI apps and registry |
| `fs/` | Block devices, MBR, FAT32, formatter, and VFS |
| `net/` | Network stack layers and protocol handlers |
| `mem/`, `mm/` | Physical, virtual, and paging management |
| `tools/` | Host-side asset and disk image tooling |
| `user/` | Userland runtime stub and shell program |

## Documentation map

- `README.md`: project overview, build flow, and release notes
- `ARCHITECTURE.md`: boot, memory, compositor, cursor, and subsystem layering
- `CHANGELOG.md`: release history for the Aurora Refresh branch
- `apps/README.md`: app integration and redraw contract notes
- `icons/README.md`: icon asset naming and launcher mapping

## Toolchain requirements

- `gcc` with 32-bit output support
- `ld`
- `grub-mkrescue`
- `qemu-system-i386`
- `python3`
- `just`

The project is freestanding and currently builds with flags centered around `-m32`, `-ffreestanding`, `-fno-pie`, `-nostdlib`, and `-nostartfiles`.

## Build and run

Rebuild from a clean tree:

```bash
just rebuild
```

Rebuild kernel and userland without resetting persistent disks:

```bash
just build-only
```

Run with persistent ATA storage and RTL8139 networking:

```bash
just run-persist
```

Run with persistent ATA storage, networking, and SB16 audio:

```bash
just run-persist-audio
```

Remove generated build outputs:

```bash
just clean
```

## Persistence workflow

coffeeOS uses two disk images during normal development:

- `build/disk.img`: a raw FAT32 image embedded into the ISO as a boot-time module
- `build/persist.img`: a raw MBR + FAT32 disk image attached as an ATA disk during persistent sessions

Typical flow:

1. Create the persistent disk once with `just mkdisk`
2. Boot with `just run-persist` or `just run-persist-audio`
3. Use `sync` or `reboot` inside coffeeOS before shutting QEMU down

## Windows VHD conversion

The persistent runtime disk is `build/persist.img`. To inspect or modify it from Windows Disk Management, convert it to a fixed VHD:

```bash
just img-to-vhd
```

That writes `build/persist.vhd`.

Convert it back for coffeeOS with:

```bash
just vhd-to-img
```

That restores `build/persist.img`.

Do not mount the VHD in Windows while QEMU is running.

## Desktop and shell notes

The desktop currently includes built-in shell-adjacent tools and always-on desktop components such as:

- Terminal
- Clock
- System Info
- Taskbar and Start menu
- Window manager and icon grid

The kernel shell and GUI terminal share the same command dispatcher. Common filesystem commands include:

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

Notepad currently supports:

- `Ctrl+S` save
- `Ctrl+N` new document
- `Ctrl+O` open by path

The Files app opens `.txt` files directly in Notepad.

## App set

The current registered GUI app set in this tree is:

- Hello App
- Calculator
- Files
- Notepad
- Paint
- Audio Mixer
- Network Monitor

Desktop-managed windows such as Terminal, Clock, and System Info are part of the shell/compositor layer rather than the dynamic app registry.

## Notes

- `just run-persist` is the standard workflow for filesystem testing across QEMU sessions
- `just run-persist-audio` adds SB16 device setup on top of the same persistent networked environment
- Both persistent run targets attach the RTL8139 NIC with QEMU user networking
- Existing packed-member warnings in FAT32 LFN decoding are known and documented in the current codebase
