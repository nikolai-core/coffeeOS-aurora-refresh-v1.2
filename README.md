# coffeeOS

coffeeOS is a 32-bit x86 hobby operating system kernel built with C and x86 assembly.

It boots through Multiboot into a higher-half 32-bit kernel, brings up the GDT/TSS/IDT path, paging, PIT, keyboard, mouse, serial, framebuffer output, and then drops straight into the desktop. The tree is still small enough that the top-level layout mostly speaks for itself: `src/boot/boot.s` gets you in, `src/kernel/kernel.c` does bring-up, `cpu/` holds descriptor tables and interrupt glue, `drivers/` is hardware-facing code, `mem/` and `mm/` cover physical and virtual memory, `audio/` is the mixer/synth layer, and `desktop/` plus `apps/` are the GUI side.

You need `gcc` with 32-bit support, `ld`, `grub-mkrescue`, `qemu-system-i386`, and `just`. Normal workflow is:

```bash
just build
just iso
just run
just clean
```

The desktop is the normal entry point. Built-in windows like Terminal, Clock, and System Info come up there, and everything else launches from desktop icons or the Start menu. App windows are created on demand instead of all opening at boot. The raw kernel shell is still there as a fallback if you leave the GUI.

The desktop side has a few extra bits now:

- app icons are baked from `icons/*.ico`
- mouse cursors are baked from `cursors/*.cur` and `cursors/*.ani`
- the build picks up every `apps/*.c` file automatically
- there is a small app framework in `include/app.h`
- audio goes through a software mixer/synth layer with SB16 output and PC speaker fallback

If you add icons or cursors, a normal `just build` regenerates the baked asset tables automatically.

For sound in QEMU, `just run` already tries to start the VM with SB16 audio enabled. If the host/QEMU combo does not like the modern audio flags, the recipe falls back to the older SB16 path. VirtualBox can boot the OS, but audio is less reliable there because the current driver support is aimed at SB16.

The raw kernel shell keeps the usual debugging commands: `help`, `ver`, `cls`, `setprompt`, `color`, `mem`, `sysinfo`, `uptime`, `history`, `echo`, `repeat`, `motd`, `dmesg`, `memmap`, `stack`, `hexdump`, `cpuinfo`, `reboot`, `panic`, and a few novelty commands. The GUI terminal routes commands through the same dispatcher, so it is the normal way to use those commands without leaving the desktop.

Everything is freestanding (`-ffreestanding`, `-nostdlib`). Text rendering is still intentionally small: Terminus-style 8x16 cells, basic ASCII, a few extra symbols, and braille block support.
