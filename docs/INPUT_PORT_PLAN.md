# INPUT_PORT_PLAN

## Scope
Map the major Windows-centric input dependencies in `projects/Main5.2/Source Main 5.2/source` that block Android porting.

## Core findings

### 1) There are **two overlapping input systems**

#### A. `CInput` (`Input.h` / `Input.cpp`)
A small wrapper that:
- stores window handle and screen size
- polls `GetCursorPos` + `ScreenToClient`
- clamps cursor to screen bounds
- derives `DX/DY`
- synthesizes L/R/M button down/held/up/double-click from `SEASON3B::IsPress/IsNone`
- disables key reads in text-edit mode

Used by some newer widgets (`Slider.cpp`, `GaugeBar.cpp`, parts of login/scene flow), but **not** the dominant input source.

#### B. Legacy global input path (`Winmain.cpp` + globals in `ZzzOpenglUtil.h`)
`WndProc` updates global state directly from Win32 messages:
- position: `MouseX`, `MouseY`
- buttons: `MouseLButton`, `MouseLButtonPush`, `MouseLButtonPop`, `MouseLButtonDBClick`
- right/middle variants
- wheel: `MouseWheel`
- focus/reset behavior on `WM_ACTIVATE`

This is the real dependency center. It is referenced broadly across UI/gameplay.

## Major dependency points

### 2) `Winmain.cpp` is the primary event ingress
Important behaviors in `WndProc`:
- `WM_MOUSEMOVE` -> converts raw window coordinates into the game's fixed 640x480 virtual space using `g_fScreenRate_x/y`
- `WM_LBUTTONDOWN/UP`, `WM_RBUTTONDOWN/UP`, `WM_MBUTTONDOWN/UP`, `WM_LBUTTONDBLCLK`
- `WM_MOUSEWHEEL`
- `WM_ACTIVATE` clears mouse state when focus is lost
- `WM_CHAR` special-cases `VK_RETURN` via `SetEnterPressed(true)`
- `WM_SETCURSOR` hides the system cursor
- window-mode capture/release on press/up

**Android implication:** the first portable seam should mimic these semantics, not raw Win32 events.

### 3) Keyboard polling is heavily tied to `GetAsyncKeyState`
There are 3 patterns:

#### A. `SEASON3B::CNewKeyInput` (`NewUICommon.cpp`)
- `ScanAsyncKeyState()` polls all 256 virtual keys each frame using `GetAsyncKeyState`
- generates `KEY_NONE / KEY_RELEASE / KEY_PRESS / KEY_REPEAT`
- backing API: `SEASON3B::IsNone/IsRelease/IsPress/IsRepeat`
- this is the most common key path for newer UI code

#### B. `PressKey(int)` in `ZzzInterface.cpp`
- edge-trigger helper built directly on `GetAsyncKeyState`
- uses `KeyState[256]`
- used for one-shot keys like F5/F6/F7, screenshot, page up/down, return in popups

#### C. Direct ad-hoc `GetAsyncKeyState(VK_*)`
Still present in gameplay/UI, especially:
- `ZzzScene.cpp`: escape/return skips, ctrl modifier, debug insert/delete, arrow-key movement/camera logic, shift modifier
- `UIControls.cpp`: list selection / cursor key handling
- `UIGuildInfo.cpp`, `GIPetManager.cpp`, some `ZzzInterface.cpp` paths

**Android implication:** do not try to port key logic file-by-file first. Replace the key backend under these APIs.

## Mouse global dependency map
Declared centrally in `ZzzOpenglUtil.h` and used pervasively.

Approximate spread from quick scan:
- `MouseX`: 190 refs / 39 files
- `MouseY`: 223 refs / 41 files
- `MouseLButtonPush`: 175 refs / 22 files
- `MouseLButton`: 76 refs / 26 files
- `MouseWheel`: 30 refs / 11 files
- right-button globals are also widespread

Common usage styles:
- hit-testing with `MouseX/MouseY`
- click edges with `MouseLButtonPush` / `MouseLButtonPop`
- drag/held logic with `MouseLButton`
- scroll lists with `MouseWheel`
- context/alternate action with `MouseRButtonPush`
- many handlers **consume** events by manually resetting globals to false/0 after use

Examples:
- `UIWindows.cpp`: move/resize windows, modal click consumption, model rotation/zoom
- `NewUIMyInventory.cpp`: pickup/drop/split/use item flows, explicit reset helpers
- `NewUIMainFrameWindow.cpp`: skill bar/list interactions
- `ZzzScene.cpp`: world click movement / scene-level interactions
- many popup/dialog windows: close button, scroll, selection

**Android implication:** portable input must preserve the current “mutable global event consumption” behavior until callers are refactored.

## Cursor warping / desktop cursor assumptions
There is no broad `ClipCursor` usage in the main client path, but there **is** cursor warping via `SetCursorPos`:
- `NewUITrade.cpp`
- `WSclient.cpp`
- `ZzzInterface.cpp`

Observed usage is UI-specific: converting between compressed UI layouts (notably widths like `260` vs base `640`) and repositioning the OS cursor after opening/closing certain dialogs.

**Android implication:**
- real cursor warping does not exist for touch
- these sites need a shim like `WarpPointerVirtual(x,y)` that, on Android, updates the virtual pointer only
- do **not** bind gameplay correctness to OS cursor APIs

## Text / IME considerations
Relevant coupling points:
- `CInput::IsKeyDown/IsKeyHeldDown` hard-disable keys when `m_bTextEditMode`
- `Winmain.cpp` handles `WM_IME_NOTIFY`
- `WM_CHAR` + enter handling is mixed into key state flow
- chat/input boxes likely need Android soft-keyboard integration rather than key polling

**Android implication:** text entry should stay separate from gameplay pointer emulation. Avoid routing soft-keyboard text through fake desktop key presses except for compatibility glue.

## Recommended Android touch -> mouse strategy

### Phase 1: compatibility-first virtual pointer
Implement a platform-neutral input backend with a per-frame snapshot plus legacy export.

Suggested minimum model:
- virtual cursor x/y in 640x480 UI space
- left/right/middle: down, pressed-this-frame, released-this-frame, double-click
- wheel delta
- key states: none/press/repeat/release
- text/IME events separated from key states
- focus/active flag

Touch mapping:
- **1 finger tap** -> `MouseLButtonPush` then `MouseLButtonPop`
- **1 finger drag** -> move virtual cursor + hold `MouseLButton`
- **1 finger hover/drag without immediate press**: optional later; not required initially
- **2 finger tap** -> right click (`MouseRButtonPush` / `MouseRButtonPop`)
- **2 finger drag** -> optional camera/scroll gesture later; avoid in phase 1
- **pinch** -> optional wheel emulation later, only where needed
- **long press** -> possible alternate right-click fallback if two-finger tap proves awkward

Practical recommendation:
- make first touch position the virtual cursor
- on press, emit the same edge/hold semantics `WndProc` currently creates
- keep cursor visible only as an in-game sprite if needed; never depend on OS cursor

## Concrete refactor order

### Step 1 — isolate backend, keep old API
Create a new `InputBackend`/`InputSnapshot` layer that owns:
- pointer position
- mouse button states
- wheel
- key states
- text input queue

Then feed legacy interfaces from it:
- `SEASON3B::IsNone/IsRelease/IsPress/IsRepeat`
- `PressKey`
- `CInput`
- global mouse vars in `ZzzOpenglUtil.h`

### Step 2 — replace `Winmain.cpp` as source of truth
Desktop path:
- Win32 messages update backend
- optional desktop polling updates key states

Android path:
- Java/NDK motion + key events update backend
- rendering/game code reads the same exported snapshot/globals

### Step 3 — kill raw `GetAsyncKeyState` callsites
Priority files:
1. `ZzzScene.cpp`
2. `UIControls.cpp`
3. `ZzzInterface.cpp`
4. any remaining direct modifier checks (`VK_SHIFT`, `VK_CONTROL`, arrows, ESC, RETURN)

Replace them with backend queries so Android no longer depends on Win32 polling.

### Step 4 — replace cursor warps with virtual warps
Introduce a helper:
- `SetVirtualCursorPos640(int x, int y)`

Desktop may optionally mirror to OS cursor.
Android should only mutate backend pointer state.

### Step 5 — separate gameplay input from text input
- keep soft keyboard/IME as explicit text events
- preserve enter/escape shortcuts only where truly needed
- avoid global keyboard polling during text edit mode

## Actionable short list
1. Add platform-neutral input snapshot + backend.
2. Export current mouse globals from that backend so existing code still runs.
3. Reimplement `SEASON3B::CNewKeyInput` on backend state instead of `GetAsyncKeyState`.
4. Reimplement `PressKey()` on backend state instead of `GetAsyncKeyState`.
5. Convert `Winmain.cpp` mouse message handlers into backend updates.
6. Add Android touch adapter that emits legacy left/right click semantics.
7. Replace `SetCursorPos` callers with virtual cursor helper.
8. After compatibility works, gradually delete direct global mouse consumers.

## Porting risk summary
- **Highest risk:** widespread mutable mouse globals and event consumption patterns.
- **Second highest:** direct `GetAsyncKeyState` usage outside the newer key wrapper.
- **Lower risk:** `CInput` itself; it is small and can become a thin wrapper.
- **Special case:** IME/text input must not be treated as normal desktop key polling.

## Bottom line
Do **not** start by rewriting every UI file. Start by emulating the exact desktop semantics behind a new backend, especially:
- 640x480 virtual cursor
- press/repeat/release key model
- push/pop mouse edges
- consumable mouse globals
- virtual cursor warp helper

That gives Android a compatibility layer first, and only then makes deeper cleanup realistic.
