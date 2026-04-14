# coffeeOS Aurora Refresh Changelog

## v1.4.0

Release focus: documentation refresh and compositor stability maintenance.

### Fixed

- Resolved the desktop cursor imprint bug caused by redraw ordering during idle clock/taskbar updates
- Fixed stale cursor background capture by forcing a self-erase before re-saving cursor backing pixels
- Added an idle cursor redraw/present fallback so the cursor does not disappear after an erase-only frame

### Updated

- Refreshed top-level documentation for the Aurora Refresh tree
- Added a dedicated architecture note covering boot flow, subsystem boundaries, compositor behavior, and cursor lifecycle
- Updated app documentation to better describe the redraw contract between app callbacks and the desktop compositor
- Updated icon documentation to match the current launcher asset set

### Notes

- This changelog reflects the current repository state and maintenance changes that are documented in-tree
- It does not claim feature work that is not yet implemented in source
