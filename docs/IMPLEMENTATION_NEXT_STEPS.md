# Implementation Next Steps

## Purpose
This document turns the existing analysis docs into a concrete short-horizon implementation sequence.

Source inputs synthesized here:
- `WINMAIN_BREAKDOWN.md`
- `INPUT_PORT_PLAN.md`
- `NETWORK_PORT_PLAN.md`
- `RENDER_PORT_PLAN.md`
- `PORTING_MATRIX.md`

The goal is **not** to solve the whole Android port in one jump. The goal is to land the next 5 coding commits in an order that:
- extracts the most important platform seams from the Windows client
- preserves legacy behavior on desktop while refactoring underneath it
- enables Android bring-up without dragging Win32/WGL/WSAAsyncSelect assumptions into the new path
- delays full renderer rewrite until the host/platform boundary is clean

---

## What the docs imply

Across all five docs, the same pattern shows up repeatedly:

1. **`Winmain.cpp` is the desktop host shell**
   - lifecycle, window/context, timers, input ingress, IME, shutdown, audio/network glue
   - conclusion: do not port it literally; carve seams around it

2. **Input is the first broad compatibility dependency**
   - mouse globals and `GetAsyncKeyState` are spread everywhere
   - conclusion: preserve semantics first, replace backend second

3. **Networking must stop depending on `WndProc`**
   - Android cannot use `WSAAsyncSelect`
   - ASIO is the likely target, but only after a transport seam exists

4. **Renderer migration is real but should not be commit #1**
   - WGL + fixed-function + immediate mode is the hardest bucket
   - conclusion: first isolate the render host/context boundary, then attack draw paths

5. **Config/timers/text input should be moved out of Win32 ownership early**
   - these are smaller than renderer, but they unlock a cleaner app shell

That means the next 5 commits should focus on **platform seam extraction and compatibility shims**, not a giant Android-specific rewrite.

---

## Recommended next 5 commits

## Commit 1 — introduce portable app/platform interfaces and move `Winmain` ownership behind them

### Commit title
`refactor: introduce platform app shell interfaces for lifecycle/config/timer/text input`

### Why first
This is the narrowest high-leverage cut through `Winmain.cpp`.

`WINMAIN_BREAKDOWN.md` shows that `Winmain.cpp` currently owns too many responsibilities. Before input, network, or render can be cleaned up safely, the project needs named seams for:
- app lifecycle
- config storage
- timer scheduling
- text input/IME control
- shutdown ordering

Without this, later Android work will keep re-embedding Win32 assumptions.

### Scope
Add new portable headers/source stubs under a new host/platform layer, for example:
- `src/platform/PlatformApp.h`
- `src/platform/PlatformConfig.h`
- `src/platform/PlatformTimer.h`
- `src/platform/PlatformTextInput.h`
- `src/platform/ShutdownCoordinator.h`

Also add a Windows implementation layer used by the existing desktop path, for example:
- `src/platform/win32/Win32PlatformApp.*`
- `src/platform/win32/Win32Config.*`
- `src/platform/win32/Win32Timer.*`
- `src/platform/win32/Win32TextInput.*`

### Concrete code changes
- Extract config read/write behind `PlatformConfig`
  - registry access stays in Win32 implementation for now
- Extract Win32 timers behind `PlatformTimer`
  - preserve `HACK_TIMER`, `CHATCONNECT_TIMER`, etc. semantics
- Extract IME/text-session control behind `PlatformTextInput`
  - keep current IME plumbing in Win32 implementation
- Add a `ShutdownCoordinator` that expresses shutdown order explicitly even if the internals still call existing teardown functions
- Make `Winmain.cpp` call these interfaces instead of owning raw platform details directly where practical

### Non-goals
- no Android implementation yet beyond empty or stubbed interfaces
- no renderer rewrite
- no network transport rewrite yet

### Done when
- `Winmain.cpp` is reduced from “god file” to a Windows host adapter invoking portable interfaces
- config/timer/text-input responsibilities are no longer conceptually hard-coded to Win32

### Main risk
- accidental behavior drift in startup/shutdown ordering

### Mitigation
- keep Windows behavior identical and extract only one seam at a time
- preserve existing call order from `WinMain`

---

## Commit 2 — add input backend + legacy export bridge

### Commit title
`refactor: add platform-neutral input backend and export legacy mouse/key state`

### Why second
`INPUT_PORT_PLAN.md` makes it clear that input is the broadest compatibility surface after `Winmain.cpp`.

A full UI rewrite is the wrong move. The correct move is to create a backend that can:
- ingest Win32 events on desktop
- ingest touch/key events on Android later
- export the exact legacy semantics the client already expects

### Scope
Introduce a new input layer, for example:
- `src/input/InputSnapshot.h`
- `src/input/InputBackend.h/.cpp`
- `src/input/LegacyInputBridge.h/.cpp`

### Concrete code changes
Backend owns:
- virtual cursor position in legacy 640x480 UI space
- button states: down / pressed-this-frame / released-this-frame / double-click
- wheel delta
- key states: none / press / repeat / release
- focus/active state
- text input queue separate from key state

Bridge exports:
- current mouse globals used across the client
- `SEASON3B::IsNone/IsRelease/IsPress/IsRepeat` compatibility source
- `PressKey()` compatibility source
- `CInput` reads from backend instead of raw Win32 polling

Desktop path:
- `WndProc` updates `InputBackend`
- direct `GetAsyncKeyState` is no longer the backend source of truth for newly bridged paths

### Important requirement
Preserve the current mutable/consumable event behavior as a compatibility layer.
That means if old UI code resets mouse globals after handling, the bridge must still support that legacy pattern for now.

### Non-goals
- do not remove every direct `GetAsyncKeyState` callsite yet
- do not introduce gestures yet beyond what the backend model can represent

### Done when
- mouse globals and key wrapper APIs are driven from one portable backend
- `Winmain.cpp` becomes just one event source, not the authoritative input model

### Main risk
- subtle click/drag/double-click regressions in legacy UI

### Mitigation
- keep desktop `WndProc` semantics byte-for-byte close to current behavior
- defer direct caller cleanup to a later commit

---

## Commit 3 — eliminate raw Win32 key polling from priority gameplay/UI paths

### Commit title
`refactor: route priority gameplay and UI key handling through input backend`

### Why third
After commit 2, the backend exists, but Android portability is still blocked if major codepaths keep calling `GetAsyncKeyState` directly.

`INPUT_PORT_PLAN.md` identifies the highest-value cleanup targets:
1. `ZzzScene.cpp`
2. `UIControls.cpp`
3. `ZzzInterface.cpp`
4. remaining modifier/arrow/escape/return hot paths

This is the first commit where portability meaningfully spreads beyond the new seam layer.

### Scope
Replace direct Win32 keyboard polling in the highest-impact callsites with backend queries.

### Concrete code changes
- Add helper queries on the backend/bridge, for example:
  - `IsKeyPressed(KeyCode)`
  - `IsKeyReleased(KeyCode)`
  - `IsKeyHeld(KeyCode)`
  - `IsModifierActive(...)`
- Replace direct polling in:
  - scene skip / camera / movement key checks
  - popup confirm/cancel keys
  - list navigation / cursor keys
  - screenshot/debug one-shots if still needed on desktop
- Add a virtual cursor warp helper such as:
  - `SetVirtualCursorPos640(int x, int y)`
- Convert existing `SetCursorPos`-style UI cursor-warps to the virtual helper where feasible

### Why this matters before network/render
This commit removes one of the biggest “desktop leaks” from gameplay/UI without requiring a renderer rewrite. It is also the minimum needed before Android touch/key adapters can map into the old logic coherently.

### Non-goals
- do not fully redesign input UX for touch yet
- do not remove all mouse globals yet

### Done when
- critical gameplay/UI paths no longer depend on raw `GetAsyncKeyState`
- keyboard behavior is sourced through the same backend Android will later feed

### Main risk
- priority hotkeys or modifier combos behave slightly differently

### Mitigation
- preserve existing press/repeat/release semantics from the `CNewKeyInput` model

---

## Commit 4 — introduce network transport seam and make main-session receive path independent of `WndProc`

### Commit title
`refactor: add client transport abstraction and detach main network flow from window messages`

### Why fourth
`NETWORK_PORT_PLAN.md` is clear: Android should be ASIO-first, but **not** as a blind replacement. The project first needs a transport seam so the main game session stops depending on `WM_ASYNCSELECTMSG`.

This commit should produce the architectural cut that later allows:
- Windows legacy transport to coexist temporarily
- Android ASIO transport to become the only mobile path

### Scope
Add a transport abstraction, for example:
- `src/network/ClientTransport.h`
- `src/network/LegacyWinsockTransport.*`
- `src/network/AsioTransport.*`
- `src/network/PacketIngress.*`

### Concrete code changes
- Define one ingress point for classic packet handling
  - e.g. `HandleIncomingClassicPacket(const uint8_t* data, int size, bool encrypted)`
- Move current main-session receive flow toward that shared ingress point
  - legacy Winsock path can still feed it
  - ASIO path can feed it directly
- Stop treating `WndProc` as the architectural center of networking
- Make the per-frame/main-loop poll call a transport facade rather than reaching into both legacy and new paths ad hoc
- Clearly mark chat-room / secondary sockets as deferred if they still depend on `CWsctlc`

### Important requirement
Do **not** attempt to finish every socket use case in this commit.
Focus on the **main game session**.

### Non-goals
- no full deletion of `WSctlc.*`
- no full `ProtocolSend` cleanup yet
- no chat-room migration yet

### Done when
- the main connection no longer conceptually depends on Win32 socket messages
- transport choice is a backend concern instead of gameplay architecture

### Main risk
- packet intake duplication or ordering bugs between old and new paths

### Mitigation
- extract one shared ingress helper and keep old decode logic intact

---

## Commit 5 — add renderer host/context abstraction and land a minimal Android-safe render bootstrap seam

### Commit title
`refactor: add renderer platform abstraction for WGL/EGL bootstrap and frame boundaries`

### Why fifth
`RENDER_PORT_PLAN.md` says the renderer is the hardest part, but there is still one safe early cut: isolate the **platform/context boundary** before touching fixed-function draw calls.

That means commit 5 should not promise a full GLES renderer. It should only create the host seam required for one later.

### Scope
Add a renderer platform layer, for example:
- `src/render/RendererPlatform.h`
- `src/render/win32/WglRendererPlatform.*`
- `src/render/android/EglRendererPlatform.*` (stub if not build-integrated yet)
- `src/render/RenderFrameLifecycle.*`

### Concrete code changes
- Move WGL/DC/context/bootstrap logic out of `Winmain.cpp`
  - `ChoosePixelFormat`
  - `SetPixelFormat`
  - `wglCreateContext`
  - `wglMakeCurrent`
  - swap/present boundary
- Define platform-neutral entry points such as:
  - `Initialize(nativeWindow, width, height)`
  - `BeginFrame()`
  - `EndFrame()`
  - `Shutdown()`
  - `OnSurfaceLost()` / `OnSurfaceRestored()` if the abstraction wants lifecycle hooks early
- Update Windows path to use the renderer platform abstraction without changing visual behavior
- Keep existing fixed-function desktop rendering alive behind the Win32/WGL implementation

### Why this is the right render commit now
It isolates the hardest desktop-only host assumptions while avoiding premature conversion of:
- immediate mode
- matrix stack
- texture env
- alpha test
- fog

Those are later commits.

### Non-goals
- no GLES shader pipeline yet
- no `RenderBitmap*()` rewrite yet
- no scene/world render port yet

### Done when
- `Winmain.cpp` no longer owns WGL setup directly
- the project has a clean place to attach EGL later

### Main risk
- context lifecycle regressions on desktop

### Mitigation
- keep the Win32/WGL implementation thin and behavior-preserving
- do not mix host abstraction work with shader migration yet

---

## Why this order is better than other obvious orders

## Why not renderer first?
Because the docs show that render work is the most expensive and most coupled to desktop assumptions. Starting there would force platform/lifecycle/input decisions to be made under pressure.

## Why not ASIO-first immediately?
Because `NETWORK_PORT_PLAN.md` explicitly shows the migration is still hybrid. A transport seam is safer than another partial side path.

## Why input before network?
Because input affects far more files immediately, and the compatibility approach is already clear: preserve semantics first, then modernize.

## Why config/timer/text-input in commit 1?
Because those concerns are all currently trapped inside `Winmain.cpp`, and extracting them early shrinks the Windows god-object before deeper subsystem work begins.

---

## Expected state after these 5 commits

If these commits land cleanly, the project should reach this state:

- `Winmain.cpp` is still present, but reduced to a Windows host adapter
- input is driven by a portable backend rather than raw Win32 state everywhere
- priority gameplay/UI key paths no longer depend on `GetAsyncKeyState`
- the main network session has a transport seam and no longer architecturally depends on `WndProc`
- render context/bootstrap has a platform abstraction ready for EGL integration

That is the right precondition for the **next wave** of work:
- Android touch adapter feeding the input backend
- Android config/path implementation
- ASIO as Android main-session transport
- GLES textured-quad path for UI
- later scene/render modernization

---

## Explicit deferrals after commit 5

These should stay out of the next 5 commits unless required for build fixes:

- full IME/mobile keyboard redesign
- full audio backend replacement
- full `ProtocolSend` decoupling from UI/global state
- chat-room/secondary socket migration
- shader conversion of the entire renderer
- immediate-mode removal across `ZzzOpenglUtil`
- full world render port in `ZzzScene`
- anti-cheat/platform integrity redesign

These are real tasks, but they are **not** the highest-leverage next five commits.

---

## Short version

If the team only wants the shortest practical answer, the next 5 coding commits should be:

1. **Platform app shell interfaces** — lifecycle/config/timer/text-input/shutdown seams out of `Winmain.cpp`
2. **Input backend** — portable snapshot + legacy mouse/key export bridge
3. **Priority input cleanup** — remove raw `GetAsyncKeyState` from major gameplay/UI paths
4. **Network transport seam** — main-session packet ingress independent of `WndProc`
5. **Renderer platform seam** — WGL bootstrap moved behind a renderer host abstraction

That order matches the dependency graph revealed by the docs and creates the cleanest runway for real Android implementation work next.
