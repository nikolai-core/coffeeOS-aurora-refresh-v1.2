# coffeeOS Aurora Refresh Changelog

## v1.4.0

Release focus: documentation refresh, compositor stability, process isolation, and kernel debugging.

### Added

- Process isolation system with per-process CR3 page directories and TSS kernel stack switching
- Process management API: process_create, process_switch_to, process_current_pid, process_mark_faulted
- Userland execution model with syscall boundary at interrupt 128
- New syscalls: SYS_GETPID (31), SYS_YIELD (32)
- Kernel panic debugger with register dumps, stack traces, process state, and CR2 on page faults
- `cpu/panic.c` and `cpu/panic.h` for comprehensive crash diagnostics

### Fixed

- Resolved the desktop cursor imprint bug caused by redraw ordering during idle clock/taskbar updates
- Fixed stale cursor background capture by forcing a self-erase before re-saving cursor backing pixels
- Added an idle cursor redraw/present fallback so the cursor does not disappear after an erase-only frame
- Fixed FAT32 LFN packed struct alignment by using memcpy into aligned local buffers instead of direct pointer access

### Updated

- Refreshed top-level documentation for the Aurora Refresh tree
- Added a dedicated architecture note covering boot flow, subsystem boundaries, compositor behavior, cursor lifecycle, process model, syscall interface, and kernel debugging
- Updated app documentation to better describe the redraw contract between app callbacks and the desktop compositor
- Updated icon documentation to match the current launcher asset set

### Notes

- This changelog reflects the current repository state and maintenance changes that are documented in-tree
- It does not claim feature work that is not yet implemented in source
