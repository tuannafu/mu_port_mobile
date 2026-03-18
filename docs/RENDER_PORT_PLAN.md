# Render Port Plan: Main5.2 -> Android / OpenGL ES

## Scope reviewed

Primary files inspected in `projects/Main5.2/Source Main 5.2/source/`:

- `ZzzOpenglUtil.cpp/.h`
- `ZzzScene.cpp`
- `ZzzTexture.cpp/.h`
- `NewUI3DRenderMng.cpp/.h`
- `UIWindows.cpp`
- `Winmain.cpp`

One extra file turned out to be structurally important for portability:

- `GlobalBitmap.cpp/.h` — this is where texture creation/destruction actually happens; `ZzzTexture.cpp` is mostly a wrapper.

---

## Executive summary

The current renderer is still fundamentally a **Windows desktop OpenGL compatibility-profile renderer** with heavy dependence on **fixed-function pipeline** behavior:

- matrix stack (`glMatrixMode`, `glPushMatrix`, `glPopMatrix`, `glLoadIdentity`)
- immediate mode (`glBegin`/`glEnd`, `glVertex*`, `glTexCoord*`, `glColor*`)
- fixed-function texturing and state (`GL_TEXTURE_2D`, `glTexEnvf`, `GL_ALPHA_TEST`, `GL_FOG`)
- WGL bootstrap and GDI-style swap setup (`ChoosePixelFormat`, `SetPixelFormat`, `wglCreateContext`, `wglMakeCurrent`)

That means this code will **not** run on Android/OpenGL ES as-is, even if the platform layer is replaced.

The good news: a **minimal Android bring-up** is still practical if the first goal is just to boot, clear, upload textures, and render a small subset of UI / simple meshes. The safest sequence is:

1. replace WGL bootstrap with EGL/ANativeWindow
2. isolate matrix/state wrappers in `ZzzOpenglUtil`
3. replace texture upload path in `GlobalBitmap`
4. introduce one tiny GLES shader pipeline for:
   - textured alpha UI
   - colored/textured simple 3D
5. emulate the old fixed-function states in engine code instead of via GL compatibility APIs

---

## Hotspots by file

## 1) `Winmain.cpp` — platform/bootstrap blocker

### Desktop-specific code found

`CreateOpenglWindow()` contains classic Win32 + WGL setup:

- `GetDC(g_hWnd)`
- `ChoosePixelFormat(...)`
- `SetPixelFormat(...)`
- `wglCreateContext(...)`
- `wglMakeCurrent(...)`
- present is tied to Win32 window/DC lifecycle

### Portability impact

This entire path is **Windows-only** and must be replaced on Android.

### Android/GLES replacement

Replace with:

- `ANativeActivity` or `GameActivity` window lifecycle
- `ANativeWindow`
- EGL display / config / surface / context creation
- `eglMakeCurrent`
- `eglSwapBuffers`

### Minimal boot target

Equivalent Android startup should do only this first:

1. wait for valid `ANativeWindow`
2. create EGL display/config for RGBA8 + depth16 or depth24
3. create GLES 2.0/3.0 context
4. call a renderer init function that sets default state
5. clear color/depth each frame
6. `eglSwapBuffers`

Do **not** try to carry over WGL multisample probing code directly.

---

## 2) `ZzzOpenglUtil.cpp` — main fixed-function compatibility wall

This file is the biggest portability hotspot.

### Immediate mode draw paths

Observed:

- `glBegin(GL_TRIANGLES)` / `glEnd()` in `BindTextureStream()`
- many `glBegin(GL_QUADS)` blocks
- many `glBegin(GL_TRIANGLE_FAN)` blocks
- per-vertex emission through `glVertex3fv`, `glTexCoord2f`, `glColor3f`, `glColor4f`, `glColor4ub`

### Why this blocks GLES

OpenGL ES has **no immediate mode** and **no `GL_QUADS`**.

### Required replacement

Convert these helpers to buffered draws:

- build small CPU-side vertex structs
- submit as triangle lists / triangle fans
- eventually move to VBOs; for first boot, client-side arrays can be used on GLES2 only via app-managed CPU buffers uploaded per draw through `glBufferData`
- all quads must become **2 triangles**

Recommended common vertex format:

```cpp
struct UiVertex {
    float pos[3];
    float uv[2];
    uint8_t color[4];
};
```

### Matrix stack dependence

Observed in multiple helpers:

- `glMatrixMode(GL_PROJECTION)`
- `glPushMatrix()` / `glPopMatrix()`
- `glLoadIdentity()`
- `glRotatef`, `glTranslatef`
- `gluPerspective`, `gluOrtho2D`
- `glGetFloatv(GL_MODELVIEW_MATRIX, ...)`

Examples:

- `BeginOpengl()`
- sprite/bitmap setup sections
- UI/3D transitions
- camera/mouse transform helpers

### Why this blocks GLES

ES 2+/3+ has **no fixed-function matrix stack** and no GLU.

### Required replacement

Create engine-owned matrices:

- `mat4 projection`
- `mat4 view`
- `mat4 model`
- `mat4 mvp`

Then:

- replace `gluPerspective2()` with a local perspective-matrix builder
- replace `gluOrtho2D()` with local orthographic-matrix builder
- replace `glPushMatrix`/`glPopMatrix` with a small C++ matrix stack if needed
- replace `glGetFloatv(GL_MODELVIEW_MATRIX, ...)` with direct reads from the engine-owned matrix

### Fixed-function state dependence

Observed wrappers and state usage:

- `glEnable(GL_TEXTURE_2D)` / `glDisable(GL_TEXTURE_2D)`
- `glEnable(GL_ALPHA_TEST)` / `glDisable(GL_ALPHA_TEST)`
- `glAlphaFunc(GL_GREATER, 0.25f)`
- `glEnable(GL_FOG)` / `glDisable(GL_FOG)`
- `glFogi(GL_FOG_MODE, GL_LINEAR)`
- `glFogf`, `glFogfv`
- blend mode wrappers with old constants:
  - additive
  - alpha blend
  - minus / lightmap style modes

### Why this blocks GLES

- `GL_TEXTURE_2D` enable/disable state is not how programmable GLES works
- `GL_ALPHA_TEST` is removed in ES 2+
- fixed-function fog is removed
- texture env/combine behavior is not automatic; shader must implement it

### Required replacement

Implement material/state policy in shaders and engine state structs:

- alpha test -> `discard` in fragment shader using a uniform threshold
- fog -> optional fragment shader branch or separate fog shader variant
- texture enable/disable -> shader uniform `useTexture`
- old blend wrappers -> map to explicit `glBlendFunc` values only

### Depth sampling / picking note

Observed:

- `glReadPixels(... GL_DEPTH_COMPONENT, GL_FLOAT, ...)`

This may work only with care on GLES and is expensive. Avoid making this a boot dependency.

### WGL extension probing inside util

Observed under `LDS_ADD_MULTISAMPLEANTIALIASING`:

- `wglGetProcAddress("wglGetExtensionsStringARB")`
- `wglGetCurrentDC()`
- `wglChoosePixelFormatARB`
- `GL_MULTISAMPLE_ARB`

This logic is desktop-only and should be removed from the portable renderer layer.

---

## 3) `ZzzScene.cpp` — frame orchestration still assumes desktop GL state machine

### Observed patterns

- projection/modelview push-pop blocks
- `gluPerspective2(...)`
- `glLoadIdentity()`
- `glRotatef(...)`
- `SwapBuffers(hDC)` / `::SwapBuffers(hDC)`
- color calls like `glColor3f`, `glColor4f`

### Portability impact

`ZzzScene.cpp` is less about bootstrap and more about **assuming compatibility-profile global state** already exists.

The scene code will not survive unchanged once matrices/colors/alpha/fog stop being implicit GL state.

### Minimal port approach

Do **not** rewrite scene logic first.

Instead:

1. preserve scene flow
2. make `BeginOpengl()` / `EndOpengl()` and related helpers produce engine-owned matrices/state
3. route draw helpers to the new GLES backend
4. replace final present call with platform abstraction:
   - desktop: `SwapBuffers` backend
   - Android: `eglSwapBuffers`

---

## 4) `NewUI3DRenderMng.cpp` — off-axis UI 3D path, moderate blocker

### Observed patterns

`CNewUI3DCamera::Render()` does:

- `EndBitmap()`
- set projection/modelview with fixed-function matrix stack
- `glViewport2(...)`
- `gluPerspective2(...)`
- `glClear(GL_DEPTH_BUFFER_BIT)`
- render 3D objects
- restore matrix stack
- `BeginBitmap()`

### Portability impact

This path is actually a good candidate for early migration because it is relatively self-contained.

### Port plan

Refactor this camera path to:

- compute its own viewport rectangle
- build `projection` and `view` matrices in CPU code
- bind a small GLES shader pipeline
- clear depth only for the subpass if needed
- render item-preview / character-preview meshes using explicit vertex submission

This can become the first successful 3D-in-UI Android path.

---

## 5) `UIWindows.cpp` — mostly piggybacks on utility layer, but still assumes fixed-function transforms

### Observed patterns

- many `glColor4ub` / `glColor4f` calls for UI tinting
- a 3D photo/render preview path with:
  - matrix push/pop
  - `gluPerspective2(...)`
  - `glRotatef`, `glTranslatef`
  - `glDisable(GL_ALPHA_TEST)`
  - `glEnable(GL_TEXTURE_2D)`
  - `glAlphaFunc(...)`
  - `glDisable(GL_FOG)`
  - `glClear(GL_DEPTH_BUFFER_BIT)`

### Portability impact

Most regular 2D UI tinting will be easy once `RenderBitmap*()` is shader-backed.

The embedded 3D preview block is a bigger issue because it depends on camera transforms and fixed-function state initialization.

### Recommended order

- migrate shared bitmap/UI draw helpers first
- then migrate the UI 3D preview to the same path used by `NewUI3DRenderMng`

---

## 6) `ZzzTexture.cpp` + `GlobalBitmap.cpp` — real texture upload blocker

`ZzzTexture.cpp` itself is mostly wrapper logic around `Bitmaps.LoadImage()` / `UnloadImage()`.
The actual upload code lives in `GlobalBitmap.cpp`.

### Observed texture upload behavior in `GlobalBitmap.cpp`

JPEG path:

- CPU decode to RGB buffer
- `glGenTextures`
- `glBindTexture(GL_TEXTURE_2D, ...)`
- `glTexImage2D(... internal=3, format=GL_RGB, type=GL_UNSIGNED_BYTE, ...)`
- `glTexParameteri(... MAG_FILTER/MIN_FILTER/WRAP_*)`

TGA path:

- CPU decode to RGBA buffer
- `glTexImage2D(... internal=4, format=GL_RGBA, ...)`
- `glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE)`
- texture parameter setup

Unload path:

- `glDeleteTextures`

### Portability impact

Most of this is easy to port, but note these issues:

1. `glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE)` is fixed-function and must disappear.
2. Internal formats `3` and `4` are legacy desktop style; GLES should use explicit `GL_RGB` / `GL_RGBA` internalFormat as supported by the target ES version.
3. There is strong dependence on old runtime texture state expectations.

### Minimal GLES replacement

Keep the same CPU decode flow, but replace upload with GLES-friendly calls:

- RGB: `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,  width, height, 0, GL_RGB,  GL_UNSIGNED_BYTE, data)`
- RGBA: `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data)`
- remove `glTexEnvf`
- ensure `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)` when needed
- prefer clamp-to-edge for UI textures

### Important side note

This project appears to pad some decoded images up to power-of-two sizes in software. GLES 2.0 generally supports NPOT with restrictions; GLES 3.x supports NPOT broadly. Since the existing code already pads, the **lowest-risk move is to preserve current padded upload behavior** at first.

---

## Fixed-function blockers to track explicitly

These are the items that must be removed or emulated before the renderer is truly portable.

## Blocker A: WGL + Win32 presentation

Must replace:

- `ChoosePixelFormat`
- `SetPixelFormat`
- `wglCreateContext`
- `wglMakeCurrent`
- `SwapBuffers(hDC)`
- WGL extension querying

With:

- EGL display/config/context/surface
- `eglMakeCurrent`
- `eglSwapBuffers`

## Blocker B: Matrix stack / GLU

Must replace:

- `glMatrixMode`
- `glPushMatrix` / `glPopMatrix`
- `glLoadIdentity`
- `glRotatef` / `glTranslatef`
- `gluPerspective`
- `gluOrtho2D`
- `glGetFloatv(GL_MODELVIEW_MATRIX, ...)`

With:

- CPU matrix math
- small matrix stack helper if needed
- shader uniforms

## Blocker C: Immediate mode and quads

Must replace:

- `glBegin` / `glEnd`
- `glVertex*`
- `glTexCoord*`
- `glColor*`
- `GL_QUADS`

With:

- vertex structs + indexed triangles
- `GL_TRIANGLES` or triangle strips/fans where appropriate

## Blocker D: Fixed-function texturing/material model

Must replace:

- `glEnable(GL_TEXTURE_2D)` / `glDisable(GL_TEXTURE_2D)`
- `glTexEnvf(GL_TEXTURE_ENV, ...)`

With:

- shaders that sample or skip sampling based on uniforms/material flags

## Blocker E: Alpha test

Must replace:

- `glEnable(GL_ALPHA_TEST)`
- `glAlphaFunc(GL_GREATER, 0.25f)`

With:

- fragment shader `discard` threshold

## Blocker F: Fog

Must replace:

- `glEnable(GL_FOG)`
- `glFogi`, `glFogf`, `glFogfv`

With:

- shader fog equation
- or temporarily disable fog during early boot

---

## Minimal Android / GLES boot strategy

This is the smallest sane path that gets visible output without trying to port the whole renderer at once.

## Phase 0 — platform shell only

Create a new renderer platform layer with interfaces like:

```cpp
bool Renderer_Init(void* nativeWindow, int width, int height);
void Renderer_BeginFrame();
void Renderer_EndFrame();
void Renderer_Shutdown();
```

Android implementation:

- owns EGL display/context/surface
- exposes surface width/height
- calls `eglSwapBuffers`

Do not expose EGL details into gameplay/UI code.

## Phase 1 — shader-backed 2D bitmap path

Target: get `RenderBitmap*()` style UI draws working first.

Implement:

- one textured-quad shader
- one untextured/tinted variant or a uniform flag
- orthographic matrix from window size
- quad emission as 2 triangles
- blend modes matching current common UI use:
  - normal alpha: `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`
  - additive: `ONE, ONE`

This removes pressure from most `UIWindows.cpp` usage quickly.

## Phase 2 — texture manager port

Port `GlobalBitmap` upload/deletion to GLES.

Keep:

- current CPU decoding paths
- current bitmap index/refcount/cache behavior

Change only:

- upload API
- texture env usage removal
- explicit unpack alignment if necessary

## Phase 3 — matrix/state abstraction in `ZzzOpenglUtil`

Turn old helpers into compatibility wrappers over a new backend state object.

Examples:

- `BeginOpengl()` should build `projection/view` matrices, not touch GL matrix stack
- `EnableAlphaBlend()` should set backend blend state, not rely on fixed-function side effects everywhere
- `glViewport2()` can remain as a wrapper around backend viewport/scissor rules

## Phase 4 — one small 3D path

Use `NewUI3DRenderMng` as first 3D validation target.

Why this is the best first 3D target:

- self-contained pass
- depth clear is localized
- fewer scene assumptions than full world render
- useful visible milestone on mobile

## Phase 5 — scene/world path

After 2D and one 3D preview work:

- port `ZzzScene` pass-by-pass
- add fog in shader
- replace any remaining immediate mode draws
- revisit depth readback/picking only after visible correctness is stable

---

## Suggested compatibility layer design

A practical short-term bridge is to keep old function names but change what they do.

Example direction:

```cpp
struct RenderState {
    mat4 projection;
    mat4 view;
    mat4 model;
    bool textureEnabled;
    bool fogEnabled;
    bool alphaTestEnabled;
    float alphaRef;
    BlendMode blend;
};
```

Then retrofit helpers:

- `EnableAlphaTest(bool)` -> updates `RenderState`, picks shader variant
- `DisableTexture()` -> sets `textureEnabled = false`
- `BeginBitmap()` -> sets ortho projection in CPU state
- `BeginOpengl()` -> sets perspective + camera view in CPU state

That lets gameplay/UI code stay mostly intact while backend behavior changes.

---

## Highest-risk areas

1. **Immediate mode density in `ZzzOpenglUtil.cpp`**
   - there are many ad-hoc draw helpers, not one central mesh path
2. **Hidden reliance on global GL state**
   - scene/UI code assumes texture/alpha/fog/depth state persists across calls
3. **`glGetFloatv`-based matrix reads**
   - once matrices are CPU-owned, any code expecting GL to be source-of-truth must be updated
4. **Desktop-only multisample/WGL code mixed into util layer**
   - should be deleted or isolated early
5. **UI and 3D passes interleave by state mutation**
   - Android port will be much easier if pass boundaries become explicit

---

## Recommended first-cut task list

1. Add portable `RendererPlatform` abstraction with desktop + Android implementations
2. Add GLES shader program for textured/tinted quads
3. Rework `GlobalBitmap` texture upload for GLES-safe formats
4. Replace `RenderBitmap*()` internals with triangle-based draw submission
5. Replace `BeginBitmap()` / `EndBitmap()` with CPU matrices
6. Replace `BeginOpengl()` / `EndOpengl()` with CPU matrices + backend state
7. Port `NewUI3DRenderMng` preview pass
8. Port `UIWindows` embedded 3D preview
9. Migrate `ZzzScene` world pass
10. Remove remaining WGL/GLU/immediate-mode dependencies

---

## Bottom line

The main portability blocker is **not texture loading** and not even `ZzzScene` by itself.
It is the combination of:

- **WGL bootstrap in `Winmain.cpp`**
- **fixed-function renderer architecture centered in `ZzzOpenglUtil.cpp`**
- **legacy texture/material assumptions carried by `GlobalBitmap.cpp` and UI helpers**

If the goal is a clean mobile bring-up, the least painful path is:

- replace platform/context first
- port texture upload second
- get shader-backed 2D working third
- port `NewUI3DRenderMng` as the first 3D proof
- only then tackle full `ZzzScene` world rendering

That order avoids trying to drag the entire compatibility-profile renderer onto Android in one shot.
