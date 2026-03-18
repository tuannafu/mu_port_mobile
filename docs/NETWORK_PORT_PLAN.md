# Main5.2 Networking Port Plan

## Verdict
An **ASIO-first migration is viable for Android**, but **not as a straight switch-flip**.

What is viable:
- making `ProtocolAsio.h` + `ProtocolSend.*` the primary game transport on Android
- preserving most gameplay packet parsing by continuing to feed received payloads into existing `TranslateProtocol(...)`
- moving all socket pumping out of Win32 `WndProc` / `WSAAsyncSelect`

What is **not** yet viable:
- compiling the current networking stack for Android unchanged
- deleting `WSctlc.*` immediately
- assuming `NEW_PROTOCOL_SYSTEM` already covers every socket use in the client

So the right call is:
- **Android: ASIO-only transport path**
- **Windows legacy client: keep `CWsctlc` until parity is proven**
- **Refactor toward a transport seam, not a blind transport replacement**

---

## Files reviewed
Primary focus:
- `Source Main 5.2/source/WSctlc.cpp`
- `Source Main 5.2/source/wsctlc.h`
- `Source Main 5.2/source/wsctlc_addon.h`
- `Source Main 5.2/source/WSclient.cpp`
- `Source Main 5.2/source/WSclient.h`
- `Source Main 5.2/source/wsclientinline.h`
- `Source Main 5.2/source/ProtocolAsio.h`
- `Source Main 5.2/source/ProtocolSend.h`
- `Source Main 5.2/source/ProtocolSend.cpp`

Supporting references checked:
- `Winmain.cpp`
- `ZzzScene.cpp`
- `ZzzInterface.cpp`
- `UIWindows.cpp`
- `Defined_Global.h`

---

## Current architecture

## 1. Legacy path: `CWsctlc` + Win32 message pump
`CWsctlc` is a Windows-only wrapper around Winsock.

### Evidence
- `Startup()` calls `WSAStartup`
- `Connect()` uses `socket`, `connect`, `gethostbyname`, and `WSAAsyncSelect`
- send buffering depends on `WSAEWOULDBLOCK`
- read flow uses `recv()` into an internal byte buffer and pushes completed packets into `CPacketQueue`
- `ProtocolCompiler(...)` drains `CWsctlc::GetReadMsg()` and decrypts/classifies packets
- `Winmain.cpp` handles socket lifecycle through window events and calls:
  - `SocketClient.nRecv()`
  - `SocketClient.FDWriteSend()`

### Portability impact
This path is **not Android-portable** in current form because it depends on:
- Winsock types/APIs
- `HWND`
- `WSAAsyncSelect`
- Windows message-driven IO assumptions

That makes `WSctlc.*` a compatibility layer for desktop Windows only.

---

## 2. New path: ASIO client transport
`ProtocolAsio.h` defines a standalone ASIO transport using:
- `asio::io_context`
- `asio::ip::tcp::socket`
- async connect/read/write
- a background thread for `io_context.run()`
- a small message wrapper (`message_header`, `message`, `owned_message`, `tsqueue`)

`ProtocolSend.*` wraps that client in `CProtocolSend` and already performs:
- server connect/disconnect
- login request send
- character-list request send
- position/move send
- receive polling via `RecvMessage()`

Most important design detail:
- for `ProtocolHead::BOTH_MESSAGE`, `ProtocolSend::RecvMessage()` copies the received payload into a raw packet buffer and calls:
  - `TranslateProtocol(head, recv, size, 0)`

That is a huge advantage for portability, because it means:
- **existing packet decoding logic in `WSclient.cpp` can stay alive**
- ASIO only needs to deliver bytes; gameplay decoding does not need a full rewrite first

---

## 3. `NEW_PROTOCOL_SYSTEM` is only a partial migration today
`Defined_Global.h` enables `NEW_PROTOCOL_SYSTEM`, but the client is still hybrid.

### What already routes to ASIO/new path
Examples:
- login: `gProtocolSend.SendRequestLogInNew(...)`
- character list: `gProtocolSend.SendRequestCharactersListNew()`
- movement/position in several UI/scene call sites:
  - `SendCharacterMoveNew(...)`
  - `SendPositionNew(...)`
- `SendPacket(...)` in `wsclientinline.h` forwards classic packet bytes through `gProtocolSend.SendPacketClassic(...)`

### What still depends on old structures / assumptions
- `SocketClient` still exists globally
- `CreateSocket()` still creates the Winsock socket first
- server handoff in `ReceiveServerConnect()` closes `SocketClient` and then opens ASIO transport only under `NEW_PROTOCOL_SYSTEM`
- `Winmain.cpp` still processes the old socket path and also polls `gProtocolSend.RecvMessage()`
- chat room / secondary sockets in `UIWindows.cpp` still use `CWsctlc`
- `ProtocolSend.cpp` keeps classic packet framing by tunneling raw packet bytes into `ProtocolHead::BOTH_MESSAGE`

So the migration is **directionally correct**, but not finished.

---

## Android viability assessment

## What makes ASIO-first viable

### 1. ASIO removes the biggest blocker: `WSAAsyncSelect`
Android cannot use the Win32 event model. ASIO already replaces that with:
- async reads/writes
- its own `io_context`
- its own worker thread

That alone makes it the only realistic base transport for Android.

### 2. Existing packet gameplay parsing can be reused
Because `ProtocolSend::RecvMessage()` feeds packets into `TranslateProtocol(...)`, the client can continue using the large receive-dispatch implementation in `WSclient.cpp` without immediately redesigning the packet layer.

This is the strongest reason to choose ASIO-first.

### 3. Most send sites already speak in packet bytes
`wsclientinline.h` is effectively the send API surface for gameplay/UI. Under `NEW_PROTOCOL_SYSTEM`, `SendPacket(...)` already reroutes classic packet bytes into `gProtocolSend.SendPacketClassic(...)`.

That means many game systems do not care whether transport is Winsock or ASIO, as long as `SendPacket(...)` works.

---

## What blocks a direct Android build today

### 1. `ProtocolAsio.h` is portable-ish, but not cleanly portable yet
It is far better than `CWsctlc`, but still has issues:
- uses `std::cout`/`std::cerr` debugging directly
- handshake and connection lifecycle are tightly embedded in the transport class
- some code is desktop-oriented rather than platform-neutral
- `DWORD`/Windows-era types leak in from `ProtocolSend.*`

This is fixable, but it is not yet “drop into NDK and done”.

### 2. `ProtocolSend.*` depends on game globals and Windows-heavy headers
`ProtocolSend.h` includes:
- `WSclient.h`
- UI/logging headers
- Windows-style types like `DWORD`, `BYTE`, `HANDLE`

`ProtocolSend.cpp` reaches into:
- `CUIMng`
- `g_pChatListBox`
- `g_GuildCache`
- `CheckHack()`
- many login/scene globals

So while transport uses ASIO, the wrapper is still strongly coupled to the Windows client runtime.

### 3. `CWsctlc` is still required for non-main transport use cases
`UIWindows.cpp` uses `CWsctlc` for chat room sockets (`m_WSClient`, `GetCurrentSocket()`, etc.).

That means an Android build cannot simply exclude `WSctlc.*` unless one of these happens:
- chat room networking is disabled for Android bring-up, or
- chat room networking also gets moved to ASIO, or
- a platform-neutral socket interface is introduced and `UIWindows` is rewritten against it

### 4. Main-loop integration is still hybrid and desktop-shaped
Today the client effectively has two receive models:
- old socket events via `Winmain.cpp`
- ASIO queue polling via `gProtocolSend.RecvMessage()`

Android should converge on only one model for gameplay networking.

---

## Recommended target architecture

## Phase A — make transport a seam
Introduce a minimal interface conceptually like:
- `INetworkTransport::connect(host, port)`
- `disconnect()`
- `isConnected()`
- `sendRawPacket(bytes, size)`
- `pollIncoming()` or callback-based delivery

Then provide:
- `LegacyWinsockTransport` backed by `CWsctlc` for Windows only
- `AsioTransport` backed by `ProtocolAsio/ProtocolSend` for Android and eventually Windows

Do **not** let gameplay/UI code touch `CWsctlc` directly except in legacy-adapter code.

---

## Phase B — keep classic packet codec, replace only transport
Short term, preserve:
- `SendPacket(...)`
- `TranslateProtocol(...)`
- most receive handlers in `WSclient.cpp`

Route them through ASIO on Android.

This gives maximum progress with minimum gameplay risk.

---

## Phase C — split `ProtocolSend` into portable transport vs game-session adapter
Current `ProtocolSend.*` mixes three concerns:
1. socket transport
2. packet tunnel / framing
3. client game-state side effects

These should be separated into:
- `AsioClientTransport` — owns socket/io thread only
- `GameSessionTransport` — converts game packets to/from transport messages
- `ClientNetworkFacade` — updates UI/global state and invokes packet decoders

This split is the key cleanup step for Android.

---

## Phase D — remove Windows-only types from portable layer
Portable networking code should stop depending on:
- `BYTE`, `WORD`, `DWORD`, `BOOL`, `HANDLE`
- `HWND`
- Win32 logging/UI headers

Replace in portable files with:
- `uint8_t`, `uint16_t`, `uint32_t`, `bool`
- platform-neutral callbacks or event sinks

---

## Phase E — handle secondary sockets explicitly
The main game connection can move first. Secondary features should be categorized:
- **must-have for first Android boot**: main game session only
- **defer**: chat room socket features using `CWsctlc` in `UIWindows.cpp`

Recommendation:
- for initial Android bring-up, **stub or disable chat-room-specific socket flows**
- migrate them later once main gameplay connection is stable

---

## Concrete migration plan

## Step 1 — freeze `CWsctlc` as legacy-only
- Treat `WSctlc.*` as a Windows compatibility backend
- Do not port it to Android
- Do not add new gameplay features to it

## Step 2 — make Android use only ASIO for the main game socket
- build `ProtocolAsio.h` / `ProtocolSend.*` in the Android-native target
- exclude `WSctlc.*` from Android main-session transport
- keep `TranslateProtocol(...)` as the decoder

## Step 3 — extract a packet ingress function
Right now `ProtocolCompiler(...)` does decrypt/framing work for legacy sockets, while `ProtocolSend::RecvMessage()` manually reconstructs raw packets for `BOTH_MESSAGE`.

Create one shared entry point such as:
- `HandleIncomingClassicPacket(const uint8_t* data, int size, bool encrypted)`

Then:
- legacy Winsock path calls it after dequeue/decrypt
- ASIO path calls it directly for tunneled classic packets

This reduces duplicated packet intake logic.

## Step 4 — decouple `ProtocolSend` from UI globals
Move out of `ProtocolSend.*`:
- UI popups
- chat-box messages
- guild-cache resets
- scene/login side effects

Replace with callbacks or higher-level facade methods.

## Step 5 — normalize send path
Long term, all gameplay sends should flow through one facade, not a mix of:
- `SocketClient`
- `g_pSocketClient`
- `gProtocolSend`
- raw socket choice in UI windows

Practical near-term goal:
- keep `wsclientinline.h` macros, but ensure they route through a transport abstraction under the hood

## Step 6 — defer chat-room socket migration
For Android milestone 1:
- disable or stub `UIWindows.cpp` chat room socket features if needed
- document them as post-login/network parity work

---

## Risk notes

## Low-risk reuse
- packet structs in `WSclient.h`
- large receive handlers in `WSclient.cpp`
- send macros in `wsclientinline.h` as temporary facade surface

## Medium-risk areas
- `ProtocolSend.cpp` because it mixes transport and UI/state
- polling cadence / main-thread ownership of received messages
- packet size/framing assumptions when tunneling classic packets through ASIO messages

## High-risk areas
- anything still expecting Win32 message timing semantics
- non-main sockets such as chat room flows
- future encryption/session validation parity if server behavior diverges between legacy and new path

---

## Recommendation to the port project
For `mu_port_mobile_clean`, the correct strategy is:

1. **Adopt ASIO as the Android networking foundation**
2. **Keep legacy packet decoding intact for now**
3. **Do not port `CWsctlc` to Android**
4. **Stub or postpone secondary socket features**
5. **Refactor `ProtocolSend` into a cleaner portable transport layer before deep Android integration**

In plain terms:
- **yes, ASIO-first is viable**
- **no, the current code is not yet clean enough to call “Android-ready”**
- **the migration should be incremental, with transport first and packet/gameplay logic second**

---

## Suggested next implementation tasks
1. Create a small `ClientTransport` abstraction in `mu_port_mobile_clean`
2. Wrap current ASIO client behind it
3. Extract shared packet ingress helper from `ProtocolCompiler(...)`
4. Move `ProtocolSend` UI/state side effects upward out of the transport layer
5. Mark chat-room sockets as deferred for Android milestone 1
