# Android (planned)

Nothing here yet. The Android app will live in this folder as a regular
Gradle project and reuse the portable code in [`../core`](../core) through the
NDK (`externalNativeBuild` → `add_subdirectory(../../core)`), so protocol
decoding stays identical on both platforms.

Platform-specific code (BLE, virtual gamepad output, UI) stays inside this
folder, just like `windows/` does for the desktop app.
