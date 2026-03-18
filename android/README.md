# Android port scaffold

This directory is a **non-destructive Android bootstrap** for the Windows-only `Source Main 5.2` client.

## Goal

Create a safe place to start an Android/NDK port without changing the existing Win32/OpenGL client tree.

## Current repo reality

The current client is tightly coupled to:

- `WinMain` / `HWND` / Win32 messages
- `windows.h`, `mmsystem.h`, `imm32`, `winmm`, `ws2_32`
- precompiled headers via `stdafx.h`
- legacy desktop OpenGL bootstrap and Windows-specific libraries
- Visual Studio project files only (`Main.sln`, `Main.vcxproj`)

Because of that, a direct Android build is **not** possible yet.

## What this scaffold adds

- Standalone Gradle Android app project under `android/`
- Native build entrypoint via CMake
- Minimal Kotlin launcher activity
- Stub native library that compiles independently from the legacy client
- Optional SDL2 integration hook for the next phase

## Proposed port layout

```text
android/
  app/
    src/main/
      AndroidManifest.xml
      java/com/main52/android/MainActivity.kt
      cpp/
        CMakeLists.txt
        native-lib.cpp
      res/values/strings.xml
    build.gradle
  build.gradle
  settings.gradle
  gradle.properties
```

## Immediate safe additions

These are safe because they do **not** modify or compile the existing Windows sources:

- Android Gradle files
- Android manifest/resources
- New NDK CMake files
- Stub native bootstrap code
- Documentation for future source extraction/refactor work

## Recommended next migration steps

1. Create a platform-neutral core layer from `Source Main 5.2/source`.
2. Isolate platform services behind interfaces:
   - window/events
   - audio
   - sockets
   - filesystem
   - timing
   - OpenGL context creation
   - input/IME
3. Replace `WinMain.cpp` responsibilities with a portable app bootstrap.
4. Introduce SDL2 as the cross-platform shell for:
   - window creation
   - GL context
   - input/touch/gamepad
   - lifecycle hooks
5. Build a small vertical slice first (boot screen + asset mount + network connect).

## SDL2 note

This scaffold does **not** vendor SDL2 yet. The next step can either:

- vendor SDL2 source under `android/third_party/SDL`, or
- consume SDL2 via a submodule/package strategy.

`app/src/main/cpp/CMakeLists.txt` is structured so that SDL2 can be wired in later with minimal churn.

## Build intent

This is currently a bootstrap shell, not a full client port.
