# Main5.2 Android Porting Matrix

## Goal
Bring the Windows client toward Android in controlled phases while preserving PC UI/logic as long as possible.

## Current strategy
- Keep Android shell isolated under `android/`
- Do **not** import the whole legacy source tree into Android build yet
- First extract platform seams from the Windows client on desktop-compatible code paths
- Then wire a minimal portable core into Android

---

## Priority 0 — already done
- Android Gradle/NDK scaffold
- Native bootstrap stub
- Initial repo assessment

---

## Priority 1 — isolate platform bootstrap

### Primary files
- `Source Main 5.2/source/Winmain.cpp`
- `Source Main 5.2/source/Winmain.h`
- `Source Main 5.2/source/stdafx.h`

### Why
This is the Windows god-object: startup, windowing, OpenGL context bootstrap, timers, shutdown, message pump, cursor, IME, and some audio/network glue all meet here.

### Deliverables
- Document responsibilities inside `Winmain.cpp`
- Split them into conceptual buckets:
  - app lifecycle
  - window/context
  - input dispatch
  - timing
  - config/registry
  - text input/IME
  - shutdown
- Define future interfaces:
  - `PlatformApp`
  - `PlatformWindow`
  - `PlatformTimer`
  - `PlatformConfig`
  - `PlatformTextInput`

### Done when
- We can describe exactly which `Winmain.cpp` code must stay Windows-only
- We have a portable entry path for future Android shell integration

---

## Priority 2 — input abstraction

### Primary files
- `Source Main 5.2/source/Input.cpp`
- `Source Main 5.2/source/Input.h`
- `Source Main 5.2/source/Winmain.cpp`
- UI/gameplay callers using `VK_*`, `GetAsyncKeyState`, mouse globals

### Why
Current client assumes desktop mouse/keyboard and Win32 event semantics.

### Deliverables
- Inventory all mouse/key globals and event entry points
- Define a platform-neutral input snapshot model
- Map Android touch -> mouse emulation strategy:
  - one finger = left click/drag
  - two-finger tap = right click
  - optional overlay keys later

### Done when
- Core UI/gameplay can read input state without direct Win32 queries

---

## Priority 3 — timing/path/config shims

### Primary files
- `Source Main 5.2/source/Winmain.cpp`
- path/config callers across source tree

### Why
Android cannot rely on Win32 registry, working-directory assumptions, or Windows timer behavior.

### Deliverables
- Replace registry-backed config assumptions with interface-backed config storage
- Normalize file/path access around one helper layer
- Centralize timing access for frame/update code

### Done when
- Config/path/timer logic can be swapped per platform

---

## Priority 4 — networking boundary

### Primary files
- `Source Main 5.2/source/WSctlc.*`
- `Source Main 5.2/source/WSclient.*`
- `Source Main 5.2/source/wsclientinline.h`
- `Source Main 5.2/source/ProtocolAsio.h`
- `Source Main 5.2/source/ProtocolSend.*`

### Why
Legacy path depends on `WSAAsyncSelect` and Windows messages.

### Deliverables
- Map old Winsock+WndProc path
- Verify whether ASIO path can become the preferred transport
- Minimize coupling between gameplay and Windows socket events

### Done when
- Client network flow no longer depends on `WndProc`

---

## Priority 5 — audio boundary

### Primary files
- `Source Main 5.2/source/DSPlaySound.*`
- `Source Main 5.2/source/DSwaveIO.*`
- `Source Main 5.2/source/Winmain.cpp` music wrappers

### Why
DirectSound and `wzAudio` are not Android-ready.

### Deliverables
- Introduce a minimal audio facade
- Allow stub/no-audio mode for early Android bring-up

### Done when
- Game can boot without Windows audio libs

---

## Priority 6 — rendering boundary (hardest)

### Primary files
- `Source Main 5.2/source/ZzzOpenglUtil.*`
- `Source Main 5.2/source/ZzzScene.cpp`
- `Source Main 5.2/source/ZzzTexture.*`
- `Source Main 5.2/source/NewUI3DRenderMng.*`
- `Source Main 5.2/source/UIWindows.cpp`

### Why
Renderer uses legacy desktop OpenGL + WGL + fixed-function/immediate mode.

### Deliverables
- Inventory WGL-specific bootstrap points
- Inventory fixed-function hot spots
- Plan GLES migration/shim layer

### Done when
- We have a minimal render boot path that does not require WGL

---

## Immediate next coding milestones
1. Push scaffold branch ✅
2. Add porting matrix ✅
3. Produce `Winmain.cpp` responsibility breakdown
4. Produce input dependency map
5. Prepare next branch/commit for platform seam extraction

---

## Notes
This project should be advanced through small reviewable commits. The fastest stable route is:
- shell first
- seams second
- Android wiring third
- renderer migration after that
