# PLATFORM_SEAM_SKELETON

## Goal

Define the **smallest non-destructive first seam** for lifting platform-specific code out of the Season 5.2 client without rewriting gameplay/UI systems yet.

This proposal is based on the current Windows-heavy choke points in:

- `Source Main 5.2/source/Winmain.cpp`
- `Source Main 5.2/source/Input.h`
- `Source Main 5.2/source/Input.cpp`
- `Source Main 5.2/source/Time/Timer.h`
- `Source Main 5.2/source/Time/Timer.cpp`

## What the current code says

### 1. App + window lifetime is concentrated in `Winmain.cpp`
`Winmain.cpp` currently owns:

- process entry (`WinMain`)
- Win32 window class registration and `CreateWindow*`
- message pump (`PeekMessage` / `GetMessage` / `DispatchMessage`)
- global `HWND/HINSTANCE/HDC/HGLRC`
- Win32 timers (`SetTimer`)
- screen/display mode changes
- config loading from both `config.ini` and registry

This makes `Winmain.cpp` the best first place to introduce a seam.

### 2. Input already has one partial wrapper, but it still leaks Win32
`CInput` is already a usable boundary for a lot of game code, but it still depends on:

- `HWND`
- `GetCursorPos`, `ScreenToClient`, `GetDoubleClickTime`
- `VK_*` and `SEASON3B::IsPress/IsRepeat/IsNone`
- `g_pTimer`

So input should get its own abstraction, but **without deleting `CInput` yet**.

### 3. Timing is split between wrapper and raw Win32 calls
There is already `CTimer`, but large parts of the client still call:

- `timeGetTime()` directly
- `SetTimer` / `KillTimer`
- `GetTickCount()` via `CTimer2`

So timer abstraction should begin as a thin interface, not a forced rewrite.

### 4. Config is not just INI
The client reads from:

- `config.ini` via `GetPrivateProfileString`
- registry via `RegCreateKeyEx` / `RegQueryValueEx`

That means the seam should be called **config/preferences**, not just “INI”.

---

## Recommendation: first skeleton files only

Add **headers/interfaces only** first. No behavior change. No bulk call-site migration. No removal of Win32 code.

Suggested new folder:

- `Source Main 5.2/source/platform/`

Suggested first files:

1. `platform/PlatformTypes.h`
2. `platform/IPlatformApp.h`
3. `platform/IPlatformWindow.h`
4. `platform/IPlatformInput.h`
5. `platform/IPlatformTimer.h`
6. `platform/IPlatformConfig.h`
7. `platform/PlatformServices.h`

This is the minimum set that gives the port a named seam for:

- app lifecycle
- window lifecycle
- input snapshot/query
- monotonic time and repeating timers
- config/preferences

---

## Why these are the minimum

### `PlatformTypes.h`
Needed so the seam can define neutral types early and stop spreading Win32 types into every new interface.

### `IPlatformApp.h`
Needed because `WinMain.cpp` is currently doing too much. This interface lets you eventually move:

- app startup
- event pumping
- quit requests

without changing gameplay code.

### `IPlatformWindow.h`
Needed because the current code hard-binds window creation and window state to Win32. Mobile will not map 1:1 to `HWND` or desktop window styles.

### `IPlatformInput.h`
Needed because `CInput` is close to being portable from the outside, but not from the inside.

### `IPlatformTimer.h`
Needed because both high-resolution frame time and coarse repeating timers exist today and are mixed together.

### `IPlatformConfig.h`
Needed because config is currently split across file + registry and will need a mobile-safe backing store.

### `PlatformServices.h`
Needed as the smallest dependency injection point. This avoids forcing globals for every interface immediately.

---

## Proposed skeletons

These are intentionally thin. They are not a full engine architecture. They are just enough to start redirecting seams later.

### 1) `platform/PlatformTypes.h`

```cpp
#pragma once

#include <stdint.h>

namespace platform
{
    struct SizeI
    {
        int width;
        int height;
    };

    struct PointI
    {
        int x;
        int y;
    };

    enum class MouseButton
    {
        Left,
        Right,
        Middle
    };

    enum class KeyState
    {
        Up,
        Pressed,
        Repeated
    };

    enum class WindowMode
    {
        Windowed,
        Fullscreen,
        Borderless
    };
}
```

### 2) `platform/IPlatformApp.h`

```cpp
#pragma once

namespace platform
{
    class IPlatformApp
    {
    public:
        virtual ~IPlatformApp() {}

        virtual bool Initialize() = 0;
        virtual bool PumpEvents() = 0;   // false => quit
        virtual void RequestQuit() = 0;
        virtual bool IsActive() const = 0;
    };
}
```

### 3) `platform/IPlatformWindow.h`

```cpp
#pragma once

#include "PlatformTypes.h"

namespace platform
{
    struct WindowCreateDesc
    {
        const char* title;
        SizeI clientSize;
        WindowMode mode;
        bool resizable;
        bool centered;
    };

    class IPlatformWindow
    {
    public:
        virtual ~IPlatformWindow() {}

        virtual bool Create(const WindowCreateDesc& desc) = 0;
        virtual void Destroy() = 0;

        virtual SizeI GetClientSize() const = 0;
        virtual bool IsActive() const = 0;
        virtual void Show() = 0;
        virtual void SetTitle(const char* title) = 0;

        // escape hatch for legacy integrations during transition
        virtual void* GetNativeHandle() const = 0;
    };
}
```

### 4) `platform/IPlatformInput.h`

```cpp
#pragma once

#include "PlatformTypes.h"

namespace platform
{
    struct InputSnapshot
    {
        PointI cursor;
        int deltaX;
        int deltaY;

        KeyState mouseLeft;
        KeyState mouseRight;
        KeyState mouseMiddle;

        bool textEditMode;
    };

    class IPlatformInput
    {
    public:
        virtual ~IPlatformInput() {}

        virtual void Update() = 0;
        virtual InputSnapshot GetSnapshot() const = 0;

        virtual bool IsKeyDown(int key) const = 0;
        virtual bool IsKeyHeld(int key) const = 0;
        virtual bool IsKeyUp(int key) const = 0;

        virtual void SetTextEditMode(bool enabled) = 0;
        virtual bool IsTextEditMode() const = 0;
        virtual double GetDoubleClickTimeMs() const = 0;
    };
}
```

### 5) `platform/IPlatformTimer.h`

```cpp
#pragma once

#include <stdint.h>

namespace platform
{
    typedef uint32_t TimerId;

    class IPlatformTimer
    {
    public:
        virtual ~IPlatformTimer() {}

        virtual double GetTimeMs() const = 0;
        virtual double GetAbsoluteTimeMs() const = 0;
        virtual void ResetFrameTime() = 0;

        virtual uint32_t GetTickCountMs() const = 0;

        virtual TimerId StartRepeatingTimer(uint32_t intervalMs) = 0;
        virtual void StopRepeatingTimer(TimerId id) = 0;
    };
}
```

### 6) `platform/IPlatformConfig.h`

```cpp
#pragma once

namespace platform
{
    class IPlatformConfig
    {
    public:
        virtual ~IPlatformConfig() {}

        virtual bool GetString(const char* section, const char* key, char* outValue, int outCapacity, const char* fallback) const = 0;
        virtual int GetInt(const char* section, const char* key, int fallback) const = 0;
        virtual bool GetBool(const char* section, const char* key, bool fallback) const = 0;

        virtual bool SetString(const char* section, const char* key, const char* value) = 0;
        virtual bool SetInt(const char* section, const char* key, int value) = 0;
        virtual bool SetBool(const char* section, const char* key, bool value) = 0;
    };
}
```

### 7) `platform/PlatformServices.h`

```cpp
#pragma once

namespace platform
{
    class IPlatformApp;
    class IPlatformWindow;
    class IPlatformInput;
    class IPlatformTimer;
    class IPlatformConfig;

    struct PlatformServices
    {
        IPlatformApp* app;
        IPlatformWindow* window;
        IPlatformInput* input;
        IPlatformTimer* timer;
        IPlatformConfig* config;
    };
}
```

---

## What not to do yet

To keep this first step non-destructive, do **not** do these in the same change:

- do not replace `WinMain.cpp` wholesale
- do not rename or remove `CInput`
- do not rewrite all `timeGetTime()` call sites
- do not convert every `SEASON3B::IsPress` use immediately
- do not force the whole client onto a service locator or smart-pointer refactor
- do not try to solve rendering/OpenGL in this seam batch

That would turn a seam introduction into a risky port branch.

---

## First migration order after headers exist

If/when implementation starts, the least risky order is:

### Step 1: wire config seam in `OpenInitFile()`
Add a Windows implementation later, then change only the config reads in `Winmain.cpp` to go through `IPlatformConfig`.

Why first:

- very localized
- low runtime risk
- easy to compare old/new behavior

### Step 2: wire timer seam for `g_pTimer`
Back `CTimer` with `IPlatformTimer` or create a Win32 adapter.

Why second:

- current code already expects a timer object
- minimal surface change

### Step 3: let `CInput` depend on `IPlatformInput`
Keep the public `CInput` API stable, but replace Win32 reads inside it later.

Why third:

- preserves most gameplay/UI call sites
- mobile input translation can happen under the wrapper

### Step 4: extract window/app control out of `Winmain.cpp`
Introduce a Win32 implementation last, because that file currently mixes startup, display mode, OpenGL, scene loop, and message pump.

Why last:

- highest coupling
- easiest place to accidentally break boot/render flow

---

## Suggested eventual Win32 implementations

Not for the first patch, but these names keep the structure obvious:

- `platform/win32/Win32PlatformApp.h`
- `platform/win32/Win32PlatformWindow.h`
- `platform/win32/Win32PlatformInput.h`
- `platform/win32/Win32PlatformTimer.h`
- `platform/win32/Win32PlatformConfig.h`

And later mobile equivalents:

- `platform/android/AndroidPlatformApp.h`
- `platform/android/AndroidPlatformWindow.h`
- `platform/android/AndroidPlatformInput.h`
- `platform/android/AndroidPlatformTimer.h`
- `platform/android/AndroidPlatformConfig.h`

---

## Concrete seam mapping from current code

| Current code | First interface target |
|---|---|
| `WinMain`, message pump, active/quit flow | `IPlatformApp` |
| `StartWindow`, `CreateWindow*`, `HWND` ownership | `IPlatformWindow` |
| `CInput::Create/Update`, cursor mapping, mouse state | `IPlatformInput` |
| `CTimer`, `CTimer2`, `timeGetTime`, `SetTimer/KillTimer` | `IPlatformTimer` |
| `GetPrivateProfileString`, registry config reads | `IPlatformConfig` |

---

## Bottom line

If only one small seam package is added first, it should be this **7-header platform skeleton**.

Why this is the right minimum:

- it matches the real choke points in the current source
- it does not force immediate rewrites
- it preserves existing game/UI code paths
- it gives the mobile port a stable place to grow adapters
- it avoids the classic mistake of mixing rendering, platform, and gameplay refactors in one pass

If I were doing the next patch after this doc, I would implement only:

1. `IPlatformConfig` Win32 adapter
2. `IPlatformTimer` Win32 adapter
3. a tiny `PlatformServices` instance wired in `Winmain.cpp`

Everything else can stay legacy until those land cleanly.
