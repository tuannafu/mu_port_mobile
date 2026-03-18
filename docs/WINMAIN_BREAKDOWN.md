# WinMain.cpp porting breakdown

Source analyzed: `/root/.openclaw/workspace/projects/Main5.2/Source Main 5.2/source/Winmain.cpp`

## Purpose of this file

`Winmain.cpp` is not just a Windows entry point. It is the desktop bootstrap and process shell for the whole client. It mixes:

- process startup and crash handling
- config loading
- display mode selection
- Win32 window creation
- OpenGL context creation
- message pump + frame loop
- mouse/keyboard/IME dispatch
- audio backend bootstrap
- socket event dispatch
- anti-tamper / anti-cheat reactions
- global teardown

For a mobile port, this file should be treated as a **host platform layer** and split apart, not translated literally.

---

## 1) Lifecycle / app bootstrap

### Key entry points

- `WinMain(...)` around lines 1318+
- `DestroyWindow()` around lines 383+
- `DestroySound()` around lines 478+
- `ExceptionCallback(...)` around lines 1305+

### What it does now

Startup sequence in `WinMain`:

1. installs exception handler via `leaf::AttachExceptionHandler(ExceptionCallback)`
2. computes/logs exe version and system info
3. parses command-line server override via `GetConnectServerInfo`
4. opens the running exe handle via `OpenMainExe()`
5. loads encryption keys (`Enc1.dat`, `Dec2.dat`)
6. loads config/registry state via `OpenInitFile()`
7. creates `CMultiLanguage`
8. optionally hides cursor
9. enumerates display modes and may switch fullscreen mode via `ChangeDisplaySettings`
10. creates window via `StartWindow`
11. creates GL context via `CreateOpenglWindow`
12. creates fonts, input system, UI system, audio, timers, gameplay/global singletons
13. initializes IME state and screen-saver behavior
14. optionally attaches system-key protection
15. enters combined message-pump + render loop
16. on quit: calls `DestroyWindow()` and returns `msg.wParam`

Shutdown path is split:

- `WM_DESTROY` does immediate runtime shutdown: close sockets, disconnect protocol, destroy sound, kill GL, close exe handle, `PostQuitMessage(0)`
- after loop exits, `DestroyWindow()` does broader object/resource cleanup

### Porting notes

- Replace the monolithic `WinMain` with platform-neutral stages:
  - `PlatformInit`
  - `LoadSettings`
  - `CreateRenderDevice`
  - `CreateGameSystems`
  - `MainLoopTick`
  - `Shutdown`
- Mobile lifecycle will be event-driven (`onCreate/onResume/onPause/onStop/onDestroy` or equivalent), not a permanent Win32 loop.
- Split teardown into deterministic ownership-based cleanup; current code relies on global singletons and duplicate shutdown responsibilities.

### Porting risk

- **High**: lifecycle logic is tightly coupled to Win32 globals, fullscreen state, GL context ownership, and anti-cheat timers.

---

## 2) Window + graphics context responsibility

### Key functions

- `StartWindow(...)` around lines 900+
- `CreateOpenglWindow()` around lines 840+
- `KillGLWindow()` around lines 177+

### What it does now

`StartWindow`:

- registers a Win32 `WNDCLASS`
- chooses fullscreen vs windowed styles through compile-time flags and `g_bUseWindowMode`
- centers or fullscreens the desktop window
- creates the HWND

`CreateOpenglWindow`:

- gets DC with `GetDC`
- picks pixel format via `ChoosePixelFormat`
- sets pixel format
- creates WGL context via `wglCreateContext`
- binds it with `wglMakeCurrent`
- shows/focuses the window

`KillGLWindow`:

- unbinds and deletes WGL context
- releases DC
- restores display settings and cursor if fullscreen was active

### Porting notes

- This entire block is **desktop-host-specific**.
- For mobile, replace with:
  - Android: `SurfaceView` / `GLSurfaceView` / native EGL layer
  - iOS/macOS mobile-style host: `CAEAGLLayer`/Metal-backed abstraction or EGL/ANGLE equivalent depending on renderer strategy
- WGL/Win32 DC concepts should disappear behind a renderer platform adapter.
- Fullscreen display mode switching via `ChangeDisplaySettings` should not carry over.
- Window style flags (`WS_...`, `WS_EX_...`) have no mobile analogue.

### Porting risk

- **High**: must be rewritten, not ported line-for-line.

---

## 3) Input dispatch responsibility

### Key code

- `WndProc(...)` around lines 502+
- mouse handling around lines 713-792
- `WM_CHAR` around lines 818+
- `CInput::Instance().Create(g_hWnd, WindowWidth, WindowHeight)` around line 1479

### What it does now

The message handler updates many global input flags and coordinates:

- activation focus state via `WM_ACTIVATE`
- mouse move -> `MouseX`, `MouseY` scaled back into legacy 640x480 UI space
- left/right/middle button down/up transitions
- double-click state
- mouse wheel delta
- enter key via `WM_CHAR` and `VK_RETURN`
- capture/release via `SetCapture` / `ReleaseCapture`

There is also legacy behavior tied to focus loss:

- resets button state when window deactivates in window mode
- uses `g_bWndActive` to gate scene rendering

### Porting notes

- The current input model is **message-based + global flags**, tuned for mouse + keyboard.
- Mobile port should define an engine-level input abstraction:
  - pointer/touch events
  - gesture mapping for right-click / double-click equivalents
  - soft keyboard text input separated from gameplay touch input
  - virtual cursor only if absolutely necessary for legacy UI
- Keep the normalized UI coordinate concept; that part is useful.
- `WM_CHAR` is not enough for mobile text entry; use platform text input APIs.

### Porting risk

- **Medium-high**: logic is simple, but assumptions are desktop-only.

---

## 4) Timers / periodic work responsibility

### Key code

- `WM_TIMER` handling around lines 552-575
- `SetTimer(g_hWnd, HACK_TIMER, 20*1000, NULL)` around line 1506
- uses of `WINDOWMINIMIZED_TIMER`, `CHATCONNECT_TIMER`, `SLIDEHELP_TIMER`

### What it does now

Win32 timers trigger:

- `HACK_TIMER` -> `CheckHack()` heartbeat
- `WINDOWMINIMIZED_TIMER` -> `PostMessage(WM_CLOSE)` delayed forced shutdown
- `CHATCONNECT_TIMER` -> chat room connection check
- `SLIDEHELP_TIMER` -> help text generation when active

### Porting notes

- Replace HWND timers with engine scheduler / task queue / platform timer services.
- Anti-cheat timer and chat-connect timer should become service-owned periodic jobs.
- Forced-close timer should become a lifecycle/state transition, not a window message.

### Porting risk

- **Medium**: behavior is straightforward, but message/timer coupling should be removed.

---

## 5) Config / registry / startup options responsibility

### Key code

- `OpenInitFile()` around lines 1034+
- command-line parsing via `Util_CheckOption(...)` and `GetConnectServerInfo(...)` around lines 1149+ and 1266+
- registry write in `DestroyWindow()` for `VolumeLevel`

### What it does now

Reads from:

- `config.ini` for login version string
- Windows registry key `HKCU\SOFTWARE\Webzen\Mu\Config`

Registry-backed values include:

- `ID`
- `SoundOnOff`
- `MusicOnOff`
- `Resolution`
- `ColorDepth`
- `TextOut`
- `WindowMode`
- `LangSelection`
- `VolumeLevel` (read/write via `leaf::CRegKey`)

Also parses command-line switches for alternate server connection info:

- `/u` + `/p`
- obfuscated `/y` + `/z`

It then derives:

- `WindowWidth` / `WindowHeight`
- `g_fScreenRate_x`, `g_fScreenRate_y`
- selected language object

### Porting notes

- Move all settings into a platform-neutral config service backed by JSON/preferences/SQLite.
- Resolution and color depth selection should become capability-based on mobile, not user registry values.
- Server override switches may still be useful for dev builds, but should be handled by modern launch args / debug settings.
- Login ID persistence must be reviewed for privacy/security on mobile.

### Porting risk

- **Medium**: easy to re-home, but several desktop-era assumptions should be deleted rather than preserved.

---

## 6) Text input / IME responsibility

### Key code

- `WM_IME_NOTIFY` around lines 794-816
- `WM_CHAR` around lines 818+
- IME init in `WinMain` around lines 1573-1578
- globals: `g_bIMEBlock`, `ActiveIME`, `g_iChatInputType`

### What it does now

The file owns a surprising amount of text-entry setup:

- creates UI text input boxes (`CUIMercenaryInputBox`, `CUITextInputBox`)
- initializes IME conversion mode with `ImmGetContext`, `ImmSetConversionStatus`, `ImmReleaseContext`
- saves IME status via `SaveIMEStatus()`
- reacts to IME notifications:
  - `IMN_SETCONVERSIONMODE`
  - `IMN_SETSENTENCEMODE`
- forwards those state changes into `CheckTextInputBoxIME(...)`
- handles Enter key from `WM_CHAR`

### Porting notes

- This must be reworked around platform text input sessions, not Win32 IME handles.
- Separate concerns:
  - engine/UI text widget state
  - platform keyboard/IME visibility and composition
  - command/submit key handling
- Mobile needs composition-string support, candidate windows owned by OS, and safe focus transitions.
- The existing in-window text box init may still be reusable conceptually, but not the Win32 IME plumbing.

### Porting risk

- **High**: East Asian text input is one of the easiest places to regress if ported superficially.

---

## 7) Audio glue responsibility

### Key code

- MP3 wrappers near top of file: `StopMp3`, `PlayMp3`, `IsEndMp3`, `GetMp3PlayPosition`
- `InitDirectSound(g_hWnd)` around line 1490
- `DestroySound()` around lines 478-484
- `wzAudioCreate`, `wzAudioOption`, `wzAudioDestroy`

### What it does now

This file is the host bootstrap for both music and effects:

- music playback through `wzAudio...`
- sound effects via `DirectSound`
- startup decides whether to init based on `m_MusicOnOff` and `m_SoundOnOff`
- volume level is restored from registry and pushed into option/effect systems
- shutdown tears down sound buffers and music system

### Porting notes

- Replace DirectSound + HWND-based audio startup with platform-neutral audio backend hooks.
- Preserve the split between:
  - background music stream control
  - effect buffer playback
  - persisted master/effect volume
- If renderer/audio backends are being modernized, this file should only call `AudioSystem::Init/Shutdown`, not backend APIs directly.

### Porting risk

- **Medium**: behavior is clear, but backend APIs are Windows-specific.

---

## 8) Networking glue responsibility

### Key code

- `WM_ASYNCSELECTMSG` handling around lines 586-621
- chat room socket message dispatch in default case around lines 703-704
- main loop protocol pumping around lines 1670-1677
- server override parsing in `GetConnectServerInfo(...)`

### What it does now

The window procedure acts as socket event dispatcher for WinSock async-select style networking:

- `FD_READ` -> `SocketClient.nRecv()`
- `FD_WRITE` -> `SocketClient.FDWriteSend()`
- `FD_CLOSE` -> logs errors, closes socket, disconnects protocol sender, raises "server lost" popup
- other custom socket messages in `[WM_CHATROOMMSG_BEGIN, WM_CHATROOMMSG_END)` route into chat room socket list

The render/message loop also manually pumps network logic every frame:

- `ProtocolCompiler()`
- `g_pChatRoomSocketList->ProtocolCompile()`
- `gProtocolSend.RecvMessage()` under `NEW_PROTOCOL_SYSTEM`

### Porting notes

- This is a classic case of **networking being coupled to a GUI window message loop**.
- For mobile, move to a dedicated network service using:
  - nonblocking sockets + poll/select/epoll/kqueue abstraction
  - or engine worker thread/event queue
- UI popups on disconnect should subscribe to network state changes, not be triggered directly in the socket callback.
- Keep server override functionality for dev/test builds if useful.

### Porting risk

- **High**: async-select + HWND messages do not map cleanly to mobile runtime architecture.

---

## 9) Shutdown / teardown responsibility

### Key code

- `WM_DESTROY` around lines 640-653
- `DestroyWindow()` around lines 383-476
- `DestroySound()` around lines 478-484
- `KillGLWindow()` around lines 177+

### What it does now

Shutdown is spread across several layers:

`WM_DESTROY`:
- sets `Destroy = true`
- closes sockets / disconnects protocol
- destroys sound
- destroys GL context
- closes exe handle
- posts quit message

`DestroyWindow()`:
- persists volume level
- releases UI manager / fonts / characters / terrain / models / bitmaps / map objects
- frees memory-dump indirection arrays
- deletes text boxes, chat room sockets, timer, movie scene, multi-language, buff/map/pet systems
- sends `WM_DESTROY` to an external window named `MuPlayer`

### Porting notes

- Current shutdown order is fragile and split between message-driven teardown and post-loop teardown.
- Refactor into owned subsystems with explicit dependencies:
  - network
  - audio
  - renderer
  - UI
  - world/assets
  - platform hooks
- The external `MuPlayer` window destroy signal is desktop-specific and should be isolated or removed.

### Porting risk

- **High**: teardown bugs will likely appear if global ownership is not cleaned up during porting.

---

## 10) Anti-cheat / protection / anti-tamper hooks

### Key code

- includes: `Nprotect.h`, `ProtectSysKey.h`, `ThemidaInclude.h`, exception handler headers
- `CheckHack()` around line 168
- `HACK_TIMER` path in `WM_TIMER`
- `WM_USER_MEMORYHACK`
- `WM_NPROTECT_EXIT_TWO`
- fullscreen minimize handling in `WM_SIZE`
- `OpenMainExe()` / `CloseMainExe()`
- checksum helpers + `GetCheckSum()` reading `data\local\Gameguard.csr`
- `KillExeProcess(...)`
- `ProtectSysKey::AttachProtectSysKey(...)`
- `VM_START/VM_END` protected regions

### What it does now

This file contains multiple protection layers:

1. **Periodic anti-hack heartbeat**
   - `SetTimer(... HACK_TIMER ...)` calls `CheckHack()` every 20 seconds.
   - `CheckHack()` sends a check packet (`SendCheck` or `gProtocolSend.SendCheckOnline`).

2. **nProtect/GameGuard-style reactions**
   - `WM_NPROTECT_EXIT_TWO` sends hacking report `0x04`, schedules forced close, shows error dialog.
   - `GetCheckSum()` calculates checksum of `data\local\Gameguard.csr`.

3. **Memory-hack / minimize reactions**
   - `WM_USER_MEMORYHACK` calls `KillGLWindow()`.
   - `WM_SIZE` minimize in protected fullscreen paths can trigger `SendHackingChecked(0x05, 0)` and mutate crypto keys from tick counts.

4. **System key suppression**
   - blocks `WM_SYSKEYDOWN`
   - may suppress `SC_KEYMENU` / `SC_SCREENSAVE`
   - calls `ProtectSysKey::AttachProtectSysKey(...)`

5. **Binary/packer protection markers**
   - `VM_START` / `VM_END` indicate Themida-protected regions around sensitive logic.

6. **Crash/exception behavior**
   - exception handler restores display settings before letting crash flow continue.

7. **Process/file protection helpers**
   - holds handle to own exe in `OpenMainExe()`
   - includes `KillExeProcess(...)` helper that enumerates processes and can terminate matching executables

### Porting notes

- Do **not** carry these hooks over mechanically.
- Separate concerns into:
  - integrity/attestation policy
  - anti-debug / anti-tamper policy
  - server-side trust model
  - lifecycle-safe failure behavior
- Mobile platforms already constrain many desktop attack surfaces, but introduce new ones.
- Anything dependent on HWND messages, fullscreen minimize detection, WGL teardown, or Windows registry/file layout must be redesigned.
- Keep protocol-visible cheat reporting semantics only if the server still depends on them.

### Porting risk

- **Very high**: this is the most platform-specific and easiest area to break accidentally.

---

## Recommended decomposition for the mobile port

Instead of one `Winmain.cpp`, split responsibilities into something like:

1. `AppBootstrap`
   - startup order, version logging, config loading
2. `PlatformHost`
   - lifecycle events, pause/resume, focus, visibility
3. `RenderHost`
   - EGL/Metal/GL context creation, surface loss/recreate
4. `InputBridge`
   - touch/pointer/key/text/IME events
5. `SettingsService`
   - persistent options, language, audio settings
6. `AudioHost`
   - music/effect backend init + shutdown
7. `NetworkPump`
   - socket event loop independent of GUI messages
8. `ProtectionAdapter`
   - optional integrity hooks, crash restore actions, server heartbeat
9. `ShutdownCoordinator`
   - deterministic subsystem shutdown order

---

## What should be preserved vs rewritten

### Preserve conceptually

- startup ordering
- settings model (not registry backend)
- normalized UI coordinate scaling
- periodic hack heartbeat semantics, if server requires them
- disconnect handling / popup behavior
- language selection flow
- audio on/off + volume settings

### Rewrite completely

- Win32 window creation
- WGL/OpenGL context setup through DC/pixel format APIs
- HWND message pump as central architecture
- async-select socket message dispatch
- IME handling through IMM APIs
- fullscreen/minimize anti-cheat assumptions
- system-key blocking and screen-saver manipulation

---

## Quick porting priority map

### First-pass extraction targets

1. config + launch options
2. lifecycle bootstrap order
3. network pumping boundary
4. input event abstraction
5. renderer host abstraction
6. audio backend abstraction
7. IME/text input isolation
8. anti-cheat hook audit

### Suggested severity by bucket

- Lifecycle: **High**
- Window/context: **High**
- Input dispatch: **Medium-High**
- Timers: **Medium**
- Config/registry: **Medium**
- Text/IME: **High**
- Audio glue: **Medium**
- Networking glue: **High**
- Shutdown: **High**
- Anti-cheat/protection: **Very High**

---

## Bottom line

`Winmain.cpp` is really a legacy **desktop platform shell**. For mobile, the right move is to mine it for responsibilities and startup/shutdown ordering, then rebuild those responsibilities behind platform-neutral interfaces. The worst thing to do here would be to preserve the HWND/message-loop architecture and try to emulate Win32 behavior on mobile.