# PSVita App Construction — Research from vita-reader

Source: `/home/minklee/minklee/IT/vita-reader` (app logic ignored, packaging/runtime pattern only).

## 1. Toolchain + Build

- `vitasdk.cmake` (also `test-app/vitasdk.cmake`):
  - `CMAKE_SYSTEM_NAME Generic`, `CMAKE_SYSTEM_PROCESSOR arm`
  - Compilers: `$VITASDK/bin/arm-vita-eabi-gcc` / `arm-vita-eabi-g++`
  - `VITASDK` env var required, `CMAKE_FIND_ROOT_PATH=$VITASDK/arm-vita-eabi`, root modes `ONLY` for lib/include.
- Official toolchain note (`CMakeLists.txt:4`):
  - Configure with `-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake`
  - Sets `-Wl,-q --emit-relocs` so `fself` gets relocation table.
- `scripts/build.sh`:
  - `cmake -S <project> -B <project>/build && cmake --build <build>`
  - Versioned VPK lands in `build/vpk/`.
- `CMakeLists.txt` basics:
  - `cmake_minimum_required(3.10)`, `project(... C CXX)`
  - `set(VITA_TITLE_ID "VITAREAD1")` — 8-char ID; `set(VITA_APP_TITLE "VitaReader")`
  - `add_definitions(-D__vita__)`, `add_compile_options(-O2 -Wall)`
  - `add_executable(App ${SOURCES})`
  - `target_include_directories`: `src/`, `src/ui`, `vendor/...`, plus SDK includes e.g. `/usr/local/vitasdk/arm-vita-eabi/include/imgui_vita2d`, `libxml2`.
  - `target_link_libraries`: `vita2d`, `freetype/png16/jpeg/bz2/z/m/pthread`, `Sce*_stub` (Display, Gxm, Ctrl, Touch, Audio, Net, NetCtl, Ssl, Http, Sysmodule, Pgf, CommonDialog, AppMgr, AppUtil, Rtc, Sysmem, KernelThreadMgr, KernelModulemgr, Iofilemgr, Processmgr, LibKernel).

## 2. Minimal Runtime Loop (`test-app/src/main.cpp`)

```cpp
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

int main() {
    vita2d_init();
    vita2d_set_vblank_wait(true);
    vita2d_texture *logo = vita2d_load_PNG_file("app0:/logo.png");
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    while (true) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        // draw...
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();
    }
    vita2d_fini();
    sceKernelExitProcess(0);
}
```

Full app additions (`src/main.cpp`):
- `sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE)` + `sceTouchSetSamplingState(FRONT, START)` before `vita2d_init()`.
- Shutdown order: app modules → `ImGui_ImplVita2D_Shutdown` → `DestroyContext` → `vita2d_fini` → `sceKernelExitProcess(0)`.

## 3. VPK Packaging Pipeline (`CMakeLists.txt` POST_BUILD)

Automated per build, mirrors in `test-app/CMakeLists.txt:58-66`:

1. `cmake -E make_directory build/sce_sys build/sce_sys/livearea/contents`
2. Copy LiveArea assets into `sce_sys/` staging.
3. `vita-mksfoex -s TITLE_ID="<ID>" "<Title>" sce_sys/param.sfo`
4. `vita-elf-create <exe> <name>.elf`
5. `vita-make-fself <name>.elf eboot.bin`
6. `vita-pack-vpk -s sce_sys/param.sfo -b eboot.bin -a <host_path>=<vpk_path> ... <name>.vpk`
7. `sh scripts/version_vpk.sh <build_dir> <out_dir>` — bumps `.vpk_count`, copies to `vpk/App_vN.vpk` for hardware bisect.

Asset bundling via `-a`:
- Main app: `vendor/fonts/Literata.ttf=Literata.ttf` (+ 4x `sce_sys/...` LiveArea files) → runtime path `app0:/Literata.ttf`.
- Test app: `logo.png=logo.png` → `app0:/logo.png`.

## 4. LiveArea (`assets/livearea/`, `scripts/gen_livearea.py`)

Files: `icon0.png (128x128)`, `bg.png (840x500)`, `startup.png (280x158)`, `template.xml`.
Minimal `template.xml`:
```xml
<livearea style="a1" format-ver="01.00" content-rev="1">
	<livearea-background><image>bg.png</image></livearea-background>
	<gate><startup-image>startup.png</startup-image></gate>
</livearea>
```
Critical: PNGs must be 8-bit indexed/palette or installer rejects with `0x8010113D`. Generator uses `Image.convert("P", palette=ADAPTIVE, colors=256)`.

## 5. Storage Paths (`src/storage.cpp`)

- Bundle (read-only): `app0:/...`
- Save data (read-write): `ux0:data/<AppName>` (e.g. `ux0:data/VitaReader`) via `sceIoMkdir / sceIoOpen / sceIoRead / sceIoWrite / sceIoRemove / sceIoGetstat`.
- `mkdirs()` creates each `ux0:` path component individually.
- Free space: `sceIoDevctl("ux0:", 0x3001, ...) → info.free_size`.
- Boot/debug log pattern (`src/main.cpp:29-37`): `sceIoMkdir`, `sceIoOpen(... O_WRONLY|O_CREAT|O_APPEND ...)`, `sceIoWrite`, `sceIoClose`.

## 6. Frame / Renderer Pattern (`src/main.cpp`, `src/renderer.cpp`)

- Landscape display: `960x544`.
- Portrait trick: `544x960` offscreen rendertarget (`vita2d_create_empty_texture_rendertarget`), presented with `vita2d_draw_texture_rotate_hotspot(target, 480, 272, PI/2, ...)` onto back buffer.
- GXM viewport does not follow rendertarget automatically — call `sceGxmSetViewport(ctx, w/2, w/2, h/2, -h/2, 0.5, 0.5)` per pass.
- Present pass must use `vita2d_start_drawing_advanced(NULL, 0)`, NOT `vita2d_start_drawing()` (latter resets ImGui vertex pool mid-frame → garbled geometry).
- ImGui backend order:
  1. `ImGui::CreateContext()`, `io.IniFilename = NULL`, flags e.g. `NavEnableGamepad`
  2. `AddFontFromFileTTF("app0:/...")` BEFORE backend init (atlas built at init, later fonts never rasterize)
  3. `ImGui_ImplVita2D_Init()`, `TouchUsage / GamepadUsage / MouseStickUsage`, analog repeat config
  4. Per frame: set `io.DisplaySize` → `vita2d_start_drawing[_advanced]` → `ImGui_ImplVita2D_NewFrame()` → UI → `ImGui::Render()` → `ImGui_ImplVita2D_RenderDrawData()` → `vita2d_end_drawing()` → `swap_buffers()`.
- Input: `sceCtrlPeekBufferPositive()` polled per frame; backend maps pad/touch to ImGui nav/mouse.

## 7. Starter Template

Copy `test-app/`: `CMakeLists.txt` + `src/main.cpp` + `vitasdk.cmake` + `scripts/version_vpk.sh`.
Change `VITA_TITLE_ID` (unique 8 chars) + `VITA_APP_TITLE`, extend `SOURCES` and link libs as needed.
