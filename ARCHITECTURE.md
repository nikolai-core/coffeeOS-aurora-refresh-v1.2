# coffeeOS Aurora Refresh v1.4.0 Architecture

This document describes the structure of the Aurora Refresh tree as it exists for v1.4.0. It is intentionally practical: it focuses on boot order, subsystem boundaries, the framebuffer compositor, the process isolation model, and the desktop redraw contract.

## System overview

coffeeOS is a freestanding 32-bit x86 operating system with:

- Multiboot entry
- Higher-half kernel mapping
- GDT and IDT setup
- Paging and physical memory management
- Per-process address spaces with CR3 switching
- TSS-based kernel stack switching for user processes
- Software framebuffer compositor
- PS/2 keyboard and mouse input
- FAT32-backed storage and VFS
- RTL8139 networking
- SB16 and speaker audio paths
- A desktop shell with built-in apps
- Kernel panic debugger with register/state dumps

## Boot path

The runtime entry sequence is:

1. `src/boot/boot.s`
   Sets up the initial CPU state expected by the kernel and transfers control to the C entry point.
2. `src/kernel/kernel.c`
   Performs top-level hardware and subsystem initialization.
3. Core setup
   GDT, IDT, paging, PMM/VMM, timer, serial, graphics, storage, input, network, and audio layers are initialized.
4. Filesystem mount
   The VFS and FAT32 layers are brought online against the available boot media and persistent media paths.
5. Desktop handoff
   The desktop shell initializes and enters its main event loop.

## Source map

| Path | Responsibility |
| --- | --- |
| `src/boot/` | Early bootstrap and Multiboot entry |
| `src/kernel/` | Kernel init, system bring-up, process management |
| `cpu/` | Descriptor tables, interrupt stubs, panic handler |
| `mem/`, `mm/` | Physical memory, virtual memory, paging |
| `drivers/` | Graphics, ATA, input, timer, serial, PCI, audio, network device drivers |
| `desktop/` | Desktop shell, window manager, taskbar, icons, compositor control |
| `apps/` | GUI apps and registry |
| `fs/` | Block device, partition, FAT32, VFS |
| `net/` | Ethernet and L3/L4 protocol stack |
| `audio/` | Higher-level audio synthesis and output helpers |
| `user/` | Userland program image and syscall-facing runtime bits |

## Process model

The kernel implements process isolation using per-process page directories and TSS-based kernel stack switching.

### Isolation mechanism

- Each process has a dedicated physical page directory (CR3)
- The Task State Segment (TSS) holds the kernel stack pointer (esp0) for the current process
- When a user process triggers an interrupt or syscalls, the CPU automatically switches to the kernel stack from TSS
- Process states: UNUSED, READY, RUNNING, EXITED, FAULTED

### Kernel panic debugger

The `cpu/panic.c` module provides comprehensive crash diagnostics:

- Register dump (EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, CS, EIP, EFLAGS, DS, ES, FS, GS, UserESP, SS)
- Current PID and process state
- Stack trace via EBP chain traversal
- CR2 register value on page faults with error code breakdown
- Both serial and framebuffer output for debugging

## Syscall interface

User processes communicate with the kernel through the syscall layer at interrupt 128 (0x80). Key syscalls include:

- `SYS_GETPID` (31): Returns the current process PID
- `SYS_YIELD` (32): Yields CPU time to other processes
- File operations: open, close, read, write, seek, mkdir, delete, stat, listdir
- Network operations: socket, bind, listen, accept, connect, send, recv, dns, ping, tcp_*

## FAT32 LFN decoding

FAT32 long filename (LFN) entries use packed UTF-16 structures. The current implementation safely decodes LFN entries by copying field data into aligned local buffers before processing, avoiding potential alignment issues with packed struct members.

## Graphics pipeline

Rendering is centered in `drivers/gfx.c`.

### Backbuffer model

- A static software backbuffer is the canonical draw surface.
- Drawing helpers update the backbuffer and mark dirty rectangles.
- `gfx_present()` copies only the dirty region to the mapped hardware framebuffer.

This avoids full-screen blits on every update and keeps window/taskbar redraws bounded.

### Dirty-rectangle flow

The normal frame lifecycle is:

1. Draw into the backbuffer
2. Mark dirty bounds with `gfx_mark_dirty()`
3. Composite the cursor if needed
4. Flush the dirty region with `gfx_present()`

The desktop shell decides whether a given frame requires:

- full scene redraw
- dirty-window redraw
- taskbar-only redraw
- cursor-only redraw

## Cursor lifecycle

The cursor is composited dynamically and is not part of the persistent desktop scene.

### Normal cursor sequence

1. `gfx_draw_cursor(x, y)` saves the pixels underneath the sprite into `cursor_saved_pixels[]`
2. The cursor sprite is blended or copied into the backbuffer
3. `gfx_present()` flushes the dirty region
4. `gfx_erase_cursor()` restores the saved pixels before another overlapping redraw

`include/gfx.h` exports `cursor_drawn` so the desktop loop can tell whether the backbuffer currently contains cursor pixels that still need to be erased or re-presented.

## v1.4.0 compositor fix

The v1.4.0 maintenance release fixes the idle cursor imprint bug by correcting three interactions between the desktop loop and the cursor compositor.

### 1. Dirty-source ordering in `desktop_run()`

The clock and system-info windows update on timed boundaries. Previously, those timed dirty sources were raised after the event loop had already decided whether the cursor needed to be erased.

That created a failure mode:

1. erase gate sees no work
2. timed code marks taskbar/window dirty
3. redraw happens over a backbuffer that still contains cursor pixels
4. `gfx_present()` flushes those cursor pixels permanently

The fix is to mark timed dirty sources first, then recompute visible dirty state, then erase the cursor.

### 2. Stale background capture in `gfx_draw_cursor()`

If the cursor was still marked as drawn and another redraw had already happened underneath it, the next `gfx_draw_cursor()` call could save cursor-colored pixels as if they were background pixels.

The fix is a self-erase guard at the top of `gfx_draw_cursor()` before new background capture begins.

### 3. Idle cursor re-present fallback

An edge case existed where the cursor could be erased on a frame that did not otherwise trigger a redraw path with a present.

The desktop loop now re-draws and presents the cursor on idle frames when `cursor_drawn == 0`.

## Desktop redraw ordering

The practical redraw contract in `desktop/desktop.c` is now:

1. Poll input and update interactive state
2. Update timed dirty sources such as clock and system-info windows
3. Recompute whether any visible window is dirty
4. Erase the cursor if scene, window, taskbar, or cursor-motion work is pending
5. Redraw the scene, windows, or taskbar as required
6. Re-composite and present the cursor

Any future periodic redraw source should follow the same rule: set dirty state before the cursor erase gate is evaluated.

## Desktop and app boundary

Apps draw through the desktop/window system rather than directly to the hardware framebuffer.

- App callbacks update app-local state
- Window invalidation is done through dirty flags
- The desktop loop controls redraw timing and final present
- The cursor is always handled after app and window content drawing

That means app code should assume redraws are scheduled, not immediate.

## Storage stack

The storage path is layered:

1. `fs/blkdev.c`
   Raw block device abstraction
2. `fs/mbr.c`
   Partition parsing
3. `fs/fat32.c`
   FAT32 volume and file operations
4. `fs/vfs.c`
   Uniform filesystem-facing API for shell and apps

The boot workflow may use a ramdisk-backed FAT32 image, while persistent sessions also attach an ATA-backed disk image.

## Network stack

The network path starts at the RTL8139 driver and rises through:

- Ethernet
- ARP
- IPv4
- ICMP
- UDP
- DHCP
- DNS
- TCP
- HTTP

The normal development path uses QEMU user networking with an RTL8139 device model.

## Audio path

Audio support is split between:

- low-level device drivers in `drivers/`
- higher-level helpers in `audio/`

The tree includes SB16 support and speaker-related output paths.

## App registry model

GUI apps are registered through `apps/app_registry.c`.

- Every `apps/*.c` file is built automatically by the current `justfile`
- The registry holds the currently exposed GUI app set
- Desktop-managed windows like Terminal, Clock, and System Info are separate from the registry

As of v1.4.0, the registered app set is:

- Hello App
- Calculator
- Files
- Notepad
- Paint
- Audio Mixer
- Network Monitor

## Operational notes

- The project is freestanding and intentionally avoids libc/runtime dependencies
- Process isolation relies on correct CR3 switching and TSS esp0 management
- The framebuffer path relies on disciplined dirty-region tracking
- Cursor correctness depends on redraw ordering, not just sprite draw/erase logic
- Persistent storage correctness depends on calling `sync` or rebooting cleanly before closing QEMU
- Kernel panics output detailed register/state information to both serial and framebuffer
