# Design: librashader shader chain for PCSX2 (phase 1, core)

Date: 2026-09-12. Base: PCSX2 `master` @ `37a8be7b4`. Reference implementation: ARMSX2 @ `859833a4b1`.
Companion analysis: `docs/superpowers/analysis/2026-09-12-librashader-post-processing-analysis.md`.

## 1. Goal and scope

Add a RetroArch `.slangp` post-processing pipeline to PCSX2 using librashader, applied to the
displayed frame only, selectable from the Qt settings UI, hot-applied without restarting the
renderer, and degrading gracefully when the library is absent.

In scope (phase 1):
- librashader 0.12.0 built by the existing deps scripts and loaded at runtime.
- `GSDevice` integration with backends Vulkan (Windows and macOS via MoltenVK), Metal, D3D11, D3D12.
- Config keys, a Shaders user folder, and a minimal Post-Processing tab UI (enable + preset picker).
- Native-resolution source, display-only placement, CAS and TV shader bypassed while active.
- Unit tests for the non-GPU parts and a manual acceptance matrix.

Out of scope (later sub-projects):
- Shader pack downloader (folder layout decisions here must stay compatible with it).
- Parameter editor UI, per-preset parameter persistence, hotkeys, OSD settings overlay entry.
- OpenGL backend (ARMSX2 port available), Linux, Intel macOS, Windows ARM64.
- Vulkan dynamic rendering (requires a 1.3 device; measured follow-up).

Decisions taken during brainstorming:
- Target is the user's own fork; upstream acceptability is not a constraint.
- Display-only placement in `VSync`, so screenshots, video capture and the ImGui OSD are unshaded.
- Native PCRTC resolution is always fed to the chain; no setting.
- librashader is built in the deps scripts, not downloaded prebuilt, and loaded via `DynamicLibrary`.
- Build targets: Windows x64 and macOS arm64.

## 2. Architecture overview

```
GSRenderer::VSync
  |- skip decision ------------------> GSDevice::NoteShaderChainFrameSkipped()
  |- CalculateDrawSrcRect/DstRect
  |- GSDevice::ApplyShaderChain(current, src_rect, src_uv, draw_rect)   [replaces CAS when active]
  |     |- downscale to native  -> m_shader_chain_source
  |     |- DoApplyShaderChain(source, target, frame_count)   [virtual, per backend]
  |     |     '- librashader <runtime>_filter_chain_frame into the device's command stream
  |     '- current = m_shader_chain_target
  |- BeginPresentFrame
  |- PresentRect(current -> swap chain, COPY shader)
  '- EndPresentFrame (FullscreenUI, ImGui OSD, EndPresent)

pcsx2/GS/ShaderChain/            (backend-agnostic, no GPU code)
  LibrashaderLoader.{h,cpp}      DynamicLibrary open, ABI check, function tables, availability
  ShaderPresets.{h,cpp}          enumerate *.slangp, relative<->absolute paths, parameter store
```

The per-backend GPU glue lives in the existing `GSDevice{VK,MTL,11,12}` classes as overrides of
`DoApplyShaderChain` / `ReleaseShaderChain`, because it needs private device operations
(ending render passes, layout transitions, state cache invalidation, descriptor heap binding).

## 3. Dependency acquisition and build

### 3.1 Deps scripts
`.github/workflows/scripts/windows/build-dependencies.bat` and
`.github/workflows/scripts/macos/build-dependencies.sh` gain a `librashader` step after shaderc:
1. Require `cargo` on PATH; fail with a clear message otherwise. Pin with
   `rustup toolchain install 1.88` and use `cargo +1.88`.
2. Clone `https://github.com/SnowflakePowered/librashader.git` at tag `librashader-v0.12.0`
   (shallow, verified by tag; the repo has no release tarball hash to check).
3. Build:
   - Windows: `cargo build -p librashader-capi --profile optimized --no-default-features --features runtime-vulkan,runtime-d3d11,runtime-d3d12`
   - macOS: `cargo build -p librashader-capi --profile optimized --no-default-features --features runtime-vulkan,runtime-metal`
4. Install: rename `librashader_capi.dll` to `deps/bin/librashader.dll` (plus `.pdb`);
   `liblibrashader_capi.dylib` to `$INSTALLDIR/lib/librashader.dylib` after
   `install_name_tool -id @rpath/librashader.dylib`. Copy `include/librashader.h` to the deps
   include dir.
5. Windows only: copy `dxcompiler.dll` from the Windows SDK (`%WindowsSdkDir%bin\<ver>\x64`)
   into `deps/bin` for the D3D12 runtime.

The CI deps cache key hashes the scripts, so it rebuilds automatically. `build-dependencies-universal.sh`
is not updated in phase 1 (macOS x86_64 out of scope); it must still succeed, so the new step is
skipped when the target arch is not the host arch.

### 3.2 CMake
- `cmake/FindLibrashader.cmake` modelled on `cmake/FindShaderc.cmake`: finds `librashader.h`
  and `librashader.dylib` / `librashader.dll`, exposes `LIBRASHADER_LIBRARY`, creates an imported
  target `Librashader::librashader` for bundling only (not linked).
- `cmake/BuildParameters.cmake`: option `USE_LIBRASHADER` default ON on Windows and Apple.
- `pcsx2/CMakeLists.txt`: when found, `target_compile_definitions(PCSX2_FLAGS INTERFACE PCSX2_HAS_LIBRASHADER=1)`
  and add `GS/ShaderChain/*.cpp`. In `setup_main_executable()`, bundle the dylib into
  `Contents/Frameworks` using the same `MACOSX_PACKAGE_LOCATION Frameworks` pattern as shaderc;
  on Windows CMake install, it is covered by the existing `deps/bin/*.dll` copy.

### 3.3 MSBuild
- `common/vsprops/LinkPCSX2Deps.props`: add `librashader.dll` and `dxcompiler.dll` to `DepsDLLs`.
- `pcsx2/pcsx2.vcxproj` and `.filters`: add the new `GS/ShaderChain` sources and headers.
- `pcsx2-qt/pcsx2-qt.vcxproj` and `.filters`: UI changes in section 6.
- Define `PCSX2_HAS_LIBRASHADER=1` in `common/vsprops/common.props` for Windows builds.

### 3.4 Header
`librashader.h` (MIT) is vendored at `3rdparty/librashader/include/librashader.h` with
`3rdparty/librashader/LICENSE` (MIT text for the header) and a `README.md` noting the pinned tag.
Sources include it via `#include "librashader.h"` with the 3rdparty include dir on the path.
The library itself is MPL-2.0 and is dynamically loaded, never linked.

### 3.5 Runtime loader (`pcsx2/GS/ShaderChain/LibrashaderLoader.{h,cpp}`)
```cpp
namespace ShaderChain {
  struct Availability { bool available; std::string reason; };
  const Availability& GetAvailability();     // loads on first call, cached, thread-safe
  const LibrashaderFunctions& Functions();   // valid only if available
}
```
- Library path: Windows `Path::Combine(EmuFolders::AppRoot, "librashader.dll")`; macOS
  `<bundle>/Contents/Frameworks/librashader.dylib` obtained via `CocoaTools` (fallback to AppRoot
  for non-bundled dev builds).
- Resolve `libra_instance_abi_version` and `libra_instance_api_version` first. Refuse unless ABI == `LIBRASHADER_CURRENT_ABI` (2).
- Function tables: `preset_*` and `error_*` common; `vk_*` always; `d3d11_*`, `d3d12_*` on
  Windows; `mtl_*` on Apple. Any missing symbol marks the library unavailable with the symbol name.
- One log line on failure. No OSD from the loader itself.

## 4. Configuration and folders

### 4.1 `Pcsx2Config::GSOptions` (`pcsx2/Config.h`, `pcsx2/Pcsx2Config.cpp`)
- `bool ShaderChainEnabled : 1;` in the bitfield union. INI key `EmuCore/GS/ShaderChainEnabled`, default false.
- `std::string ShaderChainPreset;` relative to the Shaders folder, e.g. `shaders_slang/crt/crt-royale.slangp`.
  INI key `EmuCore/GS/ShaderChainPreset`, default empty. Forward slashes stored; converted with
  `Path::` helpers when resolved.
- Added to `OptionsAreEqual` and `LoadSave`. Not added to `RestartOptionsAreEqual`: changes hot-apply.

### 4.2 `EmuFolders::Shaders`
- Default `DataRoot/shaders` (INI `Folders/Shaders = shaders`). Registered in `Config.h`,
  definitions, `SetDefaults`, `LoadConfig` (via `LoadPathFromSettings`), `EnsureFoldersExist`.
- Layout convention (shared with the future downloader): `<Shaders>/shaders_slang/...` holds the
  libretro pack; third-party packs that `#reference` it are placed under `shaders_slang/` as
  RetroArch does. Phase 1 users copy files there manually.

## 5. Core integration

### 5.1 `GSDevice` (`pcsx2/GS/Renderers/Common/GSDevice.{h,cpp}`)
Protected virtuals, no-op defaults:
```cpp
virtual bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) { return false; }
virtual void ReleaseShaderChain() {}
```
Public:
```cpp
bool ApplyShaderChain(GSTexture*& current, GSVector4i& src_rect, GSVector4& src_uv,
                      const GSVector4& draw_rect, const GSVector2i& native_size);
void NoteShaderChainFrameSkipped() { m_shader_chain_frame_count++; }
```
Members: `GSTexture* m_shader_chain_source`, `GSTexture* m_shader_chain_target`,
`u64 m_shader_chain_frame_count`. Released in `ClearCurrent()` and `Destroy()` (which also calls
`ReleaseShaderChain()`).

`ApplyShaderChain` behaviour:
1. If `!GSConfig.ShaderChainEnabled || GSConfig.ShaderChainPreset.empty() || !GetAvailability().available`:
   release textures and chain if previously active, return false.
2. Output size = `draw_rect` size rounded to integers; reject zero-size (return false).
3. `ResizeRenderTarget(&m_shader_chain_source, native_size)`; downscale `current` cropped by
   `src_rect` into it: `FilteredDownsampleTexture` for integer factors, `StretchRect(Biln)` otherwise.
4. `ResizeRenderTarget(&m_shader_chain_target, output_size)`.
5. `if (!DoApplyShaderChain(m_shader_chain_source, m_shader_chain_target, m_shader_chain_frame_count++)) return false;`
6. `current = m_shader_chain_target; src_rect = full; src_uv = {0,0,1,1}; return true`.

### 5.2 `GSRenderer::VSync` (`pcsx2/GS/Renderers/Common/GSRenderer.cpp`)
- In the skip branch (`skip_frame || ShouldSkipPresentingFrame()`), call
  `g_gs_device->NoteShaderChainFrameSkipped()`.
- Replace the CAS block with:
  ```cpp
  const bool shader_chain_active = g_gs_device->ApplyShaderChain(current, src_rect, src_uv, draw_rect,
                                                                  PCRTCDisplays.GetResolution());
  if (!shader_chain_active && GSConfig.CASMode != GSCASMode::Disabled && ...) { /* existing CAS */ }
  ```
- `PresentRect` call: when `shader_chain_active`, pass `PresentShader::COPY` and `shaderTime` 0
  instead of `s_tv_shader_indices[GSConfig.TVShader]`.
- `PresentCurrentFrame()` (resize-while-paused path) does not run the chain; it re-presents
  `m_current` unshaded. Acceptable for phase 1.

### 5.3 Chain lifecycle (inside each backend override)
- Backend keeps `void* m_shader_chain`, `std::string m_shader_chain_preset`, `bool m_shader_chain_failed`.
- On each call: if `m_shader_chain_preset != GSConfig.ShaderChainPreset`, destroy the existing
  chain, clear the failure latch, and create:
  1. `ShaderPresets::ResolvePresetPath(GSConfig.ShaderChainPreset)` -> absolute path; must exist.
  2. `libra_preset_ctx_create`, `_set_runtime(<backend>)`, `_set_core_name("PCSX2")`.
  3. `libra_preset_create_with_options(path, ctx, &opt, &preset)` with a `libra_preset_opt_t`
     whose `version = LIBRASHADER_CURRENT_VERSION` and all feature flags false. The options
     pointer must not be null: librashader only applies (and only frees) the context when it is given options.
  4. `libra_<rt>_filter_chain_create(&preset, <device args>, &opts, &chain)`; preset is consumed.
  5. On any error: `libra_error_print`/`write` into a string, `Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION, msg, Host::OSD_ERROR_DURATION)`,
     set `m_shader_chain_failed = true`, log once.
- If `m_shader_chain_failed`, return false without retrying.
- Before `_frame`: drain parameter overrides from `ShaderPresets::ParameterStore` if its generation changed.
- `ReleaseShaderChain()` and `Destroy()` free the chain after a GPU wait where the backend needs it.

### 5.4 `ShaderPresets` (`pcsx2/GS/ShaderChain/ShaderPresets.{h,cpp}`)
```cpp
namespace ShaderPresets {
  std::vector<std::string> Enumerate();                     // relative paths, sorted, '/' separators
  std::string ResolvePresetPath(std::string_view relative); // absolute, or empty if outside Shaders
  struct ParameterStore { /* mutex, preset, vector<pair<string,float>>, atomic<u64> generation */ };
  ParameterStore& Params();
}
```
- `Enumerate` uses `FileSystem::FindFiles(EmuFolders::Shaders, "*.slangp", RECURSIVE|RELATIVE_PATHS|FILES|SORT_BY_NAME)`
  and drops entries under `__MACOSX/` or starting with `.`.
- `ResolvePresetPath` rejects `..` segments and absolute input; returns `Path::Combine(EmuFolders::Shaders, relative)`.
- `ParameterStore` is written by the UI thread (future) and read by the GS thread; readers check
  the atomic generation before locking. Phase 1 ships it unused by UI.

## 6. Backend glue

Common skeleton for every override: ensure chain (5.3) -> drain params -> prepare states ->
`_frame` -> restore device state -> mark `dTex` `Dirty` -> return success. Any librashader error
returns false and logs once per preset.

### 6.1 Vulkan (`GSDeviceVK.{h,cpp}`)
- Create: `libra_device_vk_t{ m_physical_device, m_instance, m_device, m_graphics_queue, vkGetInstanceProcAddr }`;
  `filter_chain_vk_opt_t{ version = LIBRASHADER_CURRENT_VERSION, frames_in_flight = NUM_COMMAND_BUFFERS, force_no_mipmaps = false, use_dynamic_rendering = false, disable_cache = false }`.
- Frame: `EndRenderPass()`; `src->TransitionToLayout(ShaderReadOnly)`; `dst->TransitionToLayout(ColorAttachment)`;
  `libra_image_vk_t in{src->GetImage(), src->GetVkFormat(), w, h}`, `out{...}`; viewport = full target;
  `libra_vk_filter_chain_frame(&chain, GetCurrentCommandBuffer(), frame_count, in, out, &vp, nullptr, nullptr)`;
  `dst->OverrideImageLayout(ColorAttachment)`; `dst->TransitionToLayout(ShaderReadOnly)`;
  `InvalidateCachedState(); SetInitialState(GetCurrentCommandBuffer());`.
- No mid-frame submit. If testing on desktop drivers or MoltenVK shows the ARMSX2 ring-buffer
  hazard (validation error `VUID-vkDestroyImageView-imageView-01026`), fall back to
  `ExecuteCommandBuffer(false)` after the frame and record it in the spec.
- Destroy: `ExecuteCommandBuffer(true)` then `libra_vk_filter_chain_free`.

### 6.2 Metal (`GSDeviceMTL.{h,mm}`)
- Create: `libra_mtl_filter_chain_create(&preset, m_queue, nullptr, &chain)`.
- Frame: `EndRenderPass()`; `libra_mtl_filter_chain_frame(&chain, GetRenderCmdBuf(), frame_count, src->GetTexture(), dst->GetTexture(), &vp, nullptr, nullptr)`;
  `dst->SetState(Dirty)`; `FlushEncoders()` (also on failure, since passes may already be encoded).
- Header included with `#define LIBRA_RUNTIME_METAL` inside the `.mm` only; the class header keeps `void*`.

### 6.3 D3D11 (`GSDevice11.{h,cpp}`)
- Create: `libra_d3d11_filter_chain_create(&preset, m_dev.get(), &opts{version, force_no_mipmaps=false, disable_cache=false}, &chain)`.
- Frame: `libra_d3d11_filter_chain_frame(&chain, nullptr, frame_count, srv, rtv, &vp, nullptr, nullptr)` where `srv` and `rtv` come from the implicit `ID3D11ShaderResourceView*` and `ID3D11RenderTargetView*` conversion operators on `GSTexture11`.
- After: librashader restores D3D11 pipeline state; PCSX2's `m_state` cache is resynced by
  nulling `m_state.rtv/dsv`, `m_state.ps_sr_views`, viewport and scissor entries and calling
  `OMSetRenderTargets(nullptr, nullptr)` so the next binding is not skipped by the cache.

### 6.4 D3D12 (`GSDevice12.{h,cpp}`)
- Create: `libra_d3d12_filter_chain_create(&preset, m_device.get(), &opts{version, force_hlsl_pipeline=false, force_no_mipmaps=false, disable_cache=false, frames_in_flight=3}, &chain)`.
  Requires `dxcompiler.dll` beside the executable; if creation fails with a DXC error the OSD message says so.
- Frame: `EndRenderPass()`; `src->TransitionToState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)`;
  `dst->TransitionToState(D3D12_RESOURCE_STATE_RENDER_TARGET)`;
  `libra_image_d3d12_t in{ LIBRA_D3D12_IMAGE_TYPE_RESOURCE, .handle.resource = src->GetResource() }`, same for `out`;
  `libra_d3d12_filter_chain_frame(&chain, GetCommandList(), frame_count, in, out, &vp, nullptr, nullptr)`;
  `InvalidateCachedState()`; re-bind PCSX2's descriptor heaps with `SetDescriptorHeaps` on the current list.
- Destroy: `ExecuteCommandList(true)` (wait for completion) then `libra_d3d12_filter_chain_free`.

### 6.5 OpenGL, Null
No override. `GSDevice::SupportsShaderChain()` returns false there, and `ApplyShaderChain()` treats
that exactly like a disabled chain: no intermediate textures are allocated and no blit happens.
The Qt UI disables `shaderChainGroup` (tooltip and status label "The shader chain is not supported by
the OpenGL renderer.") whenever the effective renderer is OpenGL, re-evaluated on renderer change.
The Software renderer presents through a hardware device, so the chain works there and is not gated.

## 7. Qt settings UI (phase 1)

`pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui`: new `QGroupBox shaderChainGroup` "Shader Chain" containing
- `QCheckBox shaderChainEnabled` -> `BindWidgetToBoolSetting(sif, ..., "EmuCore/GS", "ShaderChainEnabled", false)`.
- `QComboBox shaderChainPreset` -> `BindWidgetToStringSetting(sif, ..., "EmuCore/GS", "ShaderChainPreset", "")`;
  items: "(None)" with empty value, then each `ShaderPresets::Enumerate()` entry as label and value.
  With `sif` set, the binder inserts "Use Global Setting" at index 0 as it does elsewhere.
- `QPushButton shaderChainRefresh` re-enumerates; `QPushButton shaderChainOpenFolder` opens `EmuFolders::Shaders`.
- `QLabel shaderChainStatus` shows `GetAvailability().reason` when unavailable, otherwise the
  last preset error if any (polled when the tab is shown).
  In phase 1 the label only reports availability (library missing, or renderer unsupported); surfacing
  the last preset error, which is only known on the GS thread, is deferred to the UX sub-project.
- When unavailable, the group is disabled with the reason as tooltip.

`GraphicsSettingsWidget.cpp`: bindings above, `populateShaderChainPresets()`, help text via `registerWidgetHelp`.
`FolderSettingsWidget.{ui,cpp}`: Shaders row following the Covers pattern.
MSBuild and CMake project files updated for any new `.ui`/`.cpp`.

## 8. Error handling summary

| Condition | Behaviour |
|---|---|
| Library missing / ABI mismatch / symbol missing | `Availability{false, reason}`; group disabled in UI; one log line; render unaffected |
| Preset path outside Shaders or missing | Treated as preset load error below |
| Preset load or chain compile error | OSD error once with librashader text; latch until preset string changes; unshaded frame |
| `_frame` error | Return false; unshaded frame; log once per preset |
| Renderer switch / device recreate | `Destroy()` frees chain and textures; lazily recreated |
| Window resize | `ResizeRenderTarget` on target; librashader adapts to new output size |
| Frame skipped | Counter advances; no GPU work |

## 9. Testing

Automated (`tests/ctest/core`, gtest):
- `ShaderPresets::Enumerate` on a temp fixture tree: nested dirs, `__MACOSX` junk, dot-files, sorting, forward slashes.
- `ResolvePresetPath`: rejects `..`, absolute paths, empty; accepts nested relative.
- `ParameterStore`: generation increments on set, readers see the latest snapshot.
- Loader: absent library yields `available == false` with a non-empty reason.

Manual acceptance matrix (all on Windows x64 and macOS arm64 development machines):

Not yet run: no PS2 BIOS or game image was available on either development machine when phase 1 was
implemented; all rows are pending.

| Backend | 1-pass | LUT preset | Feedback preset (satpixie) | 18-pass RetroCrisis |
|---|---|---|---|---|
| Vulkan (Windows) | | | | |
| Vulkan (macOS/MoltenVK) | | | | |
| Metal | | | | |
| D3D11 | | | | |
| D3D12 | | | | |

Plus, per backend: switch presets while running; toggle off/on; switch renderer with chain
active; resize window; screenshot is unshaded; OSD is crisp; delete the library and confirm
normal launch and a disabled UI group.

Success criterion: the RetroCrisis 4K preset renders correctly at full speed on all five
combinations, and PCSX2 launches and plays normally without the library.

## 10. Open items carried to later sub-projects
- Downloader: creating `<Shaders>/shaders_slang/` and placing RetroCrisis under it.
- Parameter editor and persistence using `ShaderPresets::Params()`.
- Hotkeys and settings overlay entry.
- Vulkan 1.3 bump for dynamic rendering, if profiling warrants.
- `PresentCurrentFrame` (paused resize) rendering through the chain.
