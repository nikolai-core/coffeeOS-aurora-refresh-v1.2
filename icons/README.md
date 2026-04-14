# coffeeOS Icon Assets v1.4.0

Drop `.ico` files in this directory and the asset generation step in `just build` will bake them into the desktop launcher asset table.

## Naming rules

Use filenames derived from the launcher title in lowercase hyphen form.

Examples:

- `Terminal` -> `terminal.ico`
- `Clock` -> `clock.ico`
- `System Info` -> `system-info.ico`
- `Hello App` -> `hello-app.ico`
- `Audio Mixer` -> `audio-mixer.ico`

Spaces and punctuation in titles are normalized into `-` during the host-side asset generation flow.

## Current launcher icon set

The current tree includes:

- `terminal.ico`
- `clock.ico`
- `system-info.ico`
- `hello-app.ico`
- `calculator.ico`
- `notepad.ico`
- `paint.ico`
- `audio-mixer.ico`
- `files.ico`

## Notes

- Icon assets affect desktop launcher presentation only
- App registration is still controlled separately by `apps/app_registry.c`
- If an app exists without a matching icon asset, it can still exist in the registry but may not present with the intended launcher art
