# coffeeOS Apps

Add a new app by dropping `apps/myapp.c` in the tree, defining an `App` struct, and registering the app pointer in `apps/app_registry.c`. The build already compiles every `apps/*.c` file automatically, so adding a source file does not require touching `justfile`.

App rendering goes through the helpers in [`include/app.h`](/home/nico/coffeeOS/include/app.h), so draw calls are already offset and clipped to the active client area. Desktop launchers are separate from the app build itself: if you want a proper desktop icon, add a matching `.ico` file under `icons/` using the app title turned into lowercase hyphen form. For example, `Audio Mixer` maps to `icons/audio-mixer.ico`.

The app registry still controls what shows up on the desktop and in the Start menu, so new apps need both pieces:

```c
extern App my_app;
app_registry_add(&my_app);
```

Rebuild with:

```bash
just clean
just build
just iso
just run
```
