# coffeeOS Apps Guide v1.4.0

This directory contains the built-in GUI applications that are compiled directly into the Aurora Refresh kernel image.

## App model

Each app lives in its own `apps/*.c` source file and exposes an `App` struct. The desktop and registry layer use that struct to create windows, route callbacks, invoke draw or input handlers, and apply app metadata such as stable ids, flags, and minimum sizes.

The current registry implementation lives in `apps/app_registry.c` and builds the app set in explicit order.

## Build behavior

The current `justfile` compiles every `apps/*.c` translation unit automatically, so adding a new source file does not require changing the build recipe.

Typical rebuild:

```bash
just rebuild
```

Incremental ISO refresh without recreating persistent disks:

```bash
just build-only
```

## Registration

Apps must still be exposed through the registry if you want them available through the desktop and Start menu.

Minimal pattern:

```c
extern App my_app;
app_registry_register(&my_app);
```

Descriptors should prefer designated initializers and include metadata:

```c
App my_app = {
    .title = "My App",
    .x = 80,
    .y = 80,
    .w = 360,
    .h = 240,
    .bg_color = 0x202830u,
    .on_init = my_init,
    .on_draw = my_draw,
    .on_key = my_key,
    .on_click = my_click,
    .on_close = my_close,
    .id = "my-app",
    .flags = APP_FLAG_SINGLE_INSTANCE | APP_FLAG_RESIZABLE,
    .min_w = 280,
    .min_h = 180
};
```

## Drawing contract

App rendering goes through helpers in `include/app.h`, so client-area draw calls are offset and clipped relative to the active window. Input callbacks are also invoked with the current app context set, so apps can query `app_current()`, `app_client_width()`, and `app_client_height()`.

Important rules:

- Draw only your app client area
- Mark the owning window dirty when visible content changes
- Do not assume that drawing is immediately presented to the hardware framebuffer
- Do not treat the cursor as part of your app surface
- Use shared helpers such as `app_text_width()`, `app_draw_border()`, `app_draw_button()`, `app_copy_string()`, `app_append_u32()`, and `app_format_size()` instead of duplicating fixed-font and buffer utilities in each app
- Use `app_request_redraw(app_current())` or `app_request_current_redraw()` when an app changes visible state outside a direct input callback

The desktop compositor is responsible for:

- scene redraw ordering
- taskbar redraw ordering
- cursor erase and redraw sequencing
- final `gfx_present()` calls

## Cursor and redraw notes

The v1.4.0 compositor maintenance pass matters for app authors because cursor compositing is now stricter and more predictable around redraws.

- Timed or input-driven content changes should invalidate the window and return control
- The desktop loop decides when cursor erase must happen before redraw
- The cursor is composited after your app content, not during it

That contract avoids cursor ghosts and keeps app rendering independent from sprite composition details.

## Launcher assets

Desktop launchers are separate from app compilation. If you want a proper desktop icon, add a matching `.ico` file under `icons/` using the app title converted to lowercase hyphen form.

Examples:

- `Audio Mixer` -> `icons/audio-mixer.ico`
- `Hello App` -> `icons/hello-app.ico`
- `System Info` -> `icons/system-info.ico`

## Current app set

The currently registered GUI app set in this tree is:

- Hello App
- Calculator
- Files
- Notepad
- Paint
- Audio Mixer
- Network Monitor

Desktop-managed utilities such as Terminal, Clock, and System Info are created by the desktop shell rather than the dynamic app registry.

## Recommended workflow

For persistent filesystem testing while developing apps:

```bash
just run-persist
```

For persistent app testing with audio enabled:

```bash
just run-persist-audio
```
