# Analysis: librashader post-processing pipeline and shader pack downloader for PCSX2

Date: 2026-09-12. Base: PCSX2 `master` @ `37a8be7b4`. Reference: ARMSX2 @ `859833a4b1`.
Targets: Windows (x64, ARM64 partial) and macOS (arm64, x86_64). Linux noted where it affects CI.

## 1. Verdict

Feasible with moderate effort. The ARMSX2 fork already contains a working `GSDevice`-level
integration for Vulkan, OpenGL and Metal (~1,000 lines of core C++) that ports almost
line-for-line onto upstream, because upstream's `GSRenderer::Merge` / `GSDevice` post-effect
chain is structurally identical. New work concentrates in five places:

1. Getting librashader built and shipped on Windows and macOS (new Rust dependency in the
   deps pipeline, or consuming the upstream prebuilt zips with fixes).
2. Two new backends that ARMSX2 never did: D3D11 and D3D12.
3. A Qt settings UI (preset picker, parameter editor) replacing ~2,900 lines of Kotlin/Swift.
4. A shader pack downloader: HTTP fetch, zip-to-directory extraction (does not exist today),
   a new `EmuFolders::Shaders`, and a progress dialog modelled on `CoverDownloadDialog`.
5. CI, licensing, tests and a five-backend by two-OS manual test matrix.

Estimated total: **25 to 40 engineer-days** for the full scope, **12 to 15 days** for a
minimum viable version (Vulkan + Metal + D3D11, preset picker only, single-pack downloader).
Breakdown in section 7.

## 2. What PCSX2 has today (facts)

### 2.1 Present path
- `GSRenderer::Merge(int field)` ([GSRenderer.cpp:82-262](pcsx2/GS/Renderers/Common/GSRenderer.cpp#L82-L262)):
  `Merge` -> `Interlace` -> `ShadeBoost` (:247) -> `FXAA` (:250) -> `Resize` for BilinearSharp (:255).
  Each step rewrites `GSDevice::m_current`.
- `GSRenderer::VSync` ([GSRenderer.cpp:581-852](pcsx2/GS/Renderers/Common/GSRenderer.cpp#L581-L852)):
  skip decision (:640), `CalculateDrawSrcRect/CalculateDrawDstRect` (:663-667), CAS (:670-688,
  writes to `m_cas`, not `m_current`), `BeginPresentFrame` (:691), `PresentRect(current, ...,
  nullptr /*swapchain*/, draw_rect, TVShader, ...)` (:698), `EndPresentFrame` = FullscreenUI +
  ImGui OSD + `EndPresent` (:570-579).
- Screenshots and video capture read `g_gs_device->GetCurrent()` (:1064, :834): they include
  ShadeBoost/FXAA/Interlace but exclude CAS and TV shaders.
- No legacy "ShaderFX" external shader support remains. No rotation option exists.
- Per-backend: DX11 raw OM binding with a manually patched `m_state` cache (no
  `InvalidateCachedState`); DX12 present outside `BeginRenderPass`, `InvalidateCachedState()`
  exists, descriptor heaps bound once per command list reset
  ([GSDevice12.cpp:525](pcsx2/GS/Renderers/DX12/GSDevice12.cpp#L525)); Vulkan uses classic
  render passes only, custom loader (`VKLoader.cpp`, not volk), instance API 1.1
  ([GSDeviceVK.cpp:113](pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp#L113)), `InvalidateCachedState()`
  present; OpenGL global `GLState` cache with early-return setters; Metal ObjC++ with
  `CAMetalLayer`, encoder-scoped state, `EndRenderPass`/`FlushEncoders`.

### 2.2 Config and settings flow
- `Pcsx2Config::GSOptions` post-processing fields: `FXAA`, `ShadeBoost` bits, `CASMode`,
  `CAS_Sharpness`, `TVShader`, `ShadeBoost_*`, `LinearPresent` ([Config.h:701-968](pcsx2/Config.h#L701-L968)).
- Post-processing options are hot-applied: `RestartOptionsAreEqual`
  ([Pcsx2Config.cpp:911-924](pcsx2/Pcsx2Config.cpp#L911-L924)) only lists renderer/adapter/debug
  flags, and `GSUpdateConfig` ([GS.cpp:822-918](pcsx2/GS/GS.cpp#L822-L918)) has no handler for
  them; `Merge`/`VSync` read `GSConfig.*` every frame.
- Hotkey exemplar: `CycleTVShader` ([GS.cpp:1347-1371](pcsx2/GS/GS.cpp#L1347-L1371)) mutates
  `EmuConfig.GS` and `GSConfig` via `MTGS::RunOnGSThread`, posts `Host::AddKeyedOSDMessage`.
- Frame counter on GS thread: `g_perfmon.GetFrame()`; `GSDevice::m_frame` is pool-ageing only.

### 2.3 Qt UI
- Post-Processing tab: `GraphicsPostProcessingSettingsTab.ui` bound in
  [GraphicsSettingsWidget.cpp:210-223](pcsx2-qt/Settings/GraphicsSettingsWidget.cpp#L210-L223)
  via `SettingWidgetBinder`. Per-game overlay via `SettingsWindow::isPerGameSettings()`.
- Dynamic combobox population examples: adapters (:1120-1155), audio drivers, BIOS list
  (`FileSystem::FindFiles`).
- Progress dialog exemplar: `CoverDownloadDialog` (181 lines) with a nested
  `QtAsyncProgressThread` subclass whose `runAsync()` calls `GameList::DownloadCovers`.

### 2.4 Download and archive infrastructure
- `common/HTTPDownloader` (curl on macOS/Linux, WinHTTP on Windows): whole body buffered in
  memory, no size cap, per-request byte progress via `ProgressCallback`, cancellation, caller
  must `PollRequests()`. Users: Achievements, AutoUpdater, `QtHost::DownloadFile` (modal),
  `GameList::DownloadCovers`.
- libzip is vendored and linked into the core only. `common/ZipHelpers.h` reads single entries
  into memory. **No zip-to-directory-tree extractor exists** (the Windows updater uses 7z/LZMA).
- `EmuFolders` has no `Shaders` entry; built-in GS shaders live in `bin/resources/shaders`.
  Registering a folder touches `Config.h`, `Pcsx2Config.cpp` (`SetDefaults`, `LoadConfig`,
  `EnsureFoldersExist`) and optionally `FolderSettingsWidget`.

### 2.5 Build system
- Windows: MSBuild `PCSX2_qt.slnx` (MSVC and clang-cl, x64 + ARM64) plus a CI-tested CMake path.
  Prebuilt deps come from `scripts/windows/build-dependencies.bat` into `deps/` (all shared
  libs, SHA-checked curl downloads), consumed via `common/vsprops/DepsDir.props` and
  `LinkPCSX2Deps.props` (DLL copy list). ARM64 deps script exists but has no CI job.
- macOS: CMake, deployment target 11.0, two single-arch apps (x86_64 and arm64), deps built by
  `scripts/macos/build-dependencies.sh` (cached on script hash). Prebuilt shared lib pattern:
  `cmake/FindShaderc.cmake` + `MACOSX_PACKAGE_LOCATION Frameworks` + `macdeployqt` post-process
  ([pcsx2/CMakeLists.txt:1404-1433](pcsx2/CMakeLists.txt#L1404-L1433)).
- No Rust toolchain, no ExternalProject/FetchContent, no corrosion anywhere. The only cargo use
  is installing `dump_syms` in Windows CI.
- Flatpak uses `org.kde.Sdk` 6.11 with only the llvm extension; a Rust SDK extension would be
  needed if Linux builds librashader from source.

## 3. What ARMSX2 did (reusable reference)

| Area | ARMSX2 approach | Portable to upstream? |
|---|---|---|
| Obtain librashader | `FetchContent` of the repo at tag `librashader-cache-v0.12.0`, `cargo build -p librashader-capi --no-default-features --features ...` at configure time; shared on Android, static on iOS; links `librashader.h` directly | Pattern is Android/iOS-app CMake; upstream needs a deps-script or prebuilt approach instead (section 5.1) |
| Config | `GSOptions::ShaderChainEnabled`, `ShaderChainPreset` (absolute path), INI keys `EmuCore/GS/ShaderChain*` | Yes, verbatim |
| GSDevice API | `virtual bool DoApplyShaderChain(GSTexture*, GSTexture*, size_t frame_count)` and `ReleaseShaderChain()` with no-op defaults; `ApplyShaderChain(output_size, source_size)`; static param store with atomic generation counter for UI-thread writes | Yes, verbatim |
| Hook point | Inside `Merge`, after FXAA, before BilinearSharp resize; downsamples upscaled frame to native PCRTC resolution first; output to dedicated `m_shader_chain_target` sized to the aspect-corrected draw rect; presenter blits 1:1 | Yes; upstream `Merge` has the same sequence at :247-255 |
| Frame count | Owned by `GSDevice`, incremented on skipped frames too (`NoteShaderChainFrameSkipped`) | Yes |
| Vulkan | `libra_device_vk_t{physical_device, instance, device, queue, entry=vkGetInstanceProcAddr}`, `frames_in_flight=0` (3), dynamic rendering probed, `EndRenderPass` then layout transitions then `libra_vk_filter_chain_frame` on the current command buffer, then **unconditional `ExecuteCommandBuffer(false)`** (Adreno workaround) | Mostly; the submit workaround should be replaced on desktop by `InvalidateCachedState()` + `SetInitialState()` (tried in ARMSX2, reverted only because of Adreno) |
| OpenGL | file-static proc loader, `use_dsa=false`, `glsl_version=0`, `RestoreGLStateAfterShaderChain()` re-pushing `GLState` | Yes; set `use_dsa=true` on desktop (PCSX2 GL already requires DSA) which also enables the cache |
| Metal | `libra_mtl_filter_chain_create(&preset, m_queue, nullptr, &chain)`, `EndRenderPass`, frame on `GetRenderCmdBuf()`, `FlushEncoders()` after | Yes, verbatim |
| Perf | Skip chain on unpresented frames via ARMSX2-only `MergeMode`; dedicated textures; dynamic rendering | Needs an upstream-specific early-out (section 5.2) |
| D3D11 / D3D12 | Not done | New work |
| UI | Kotlin + Swift, ~2,900 lines; params via `libra_preset_get_runtime_params` on a separately loaded preset handle | Replace with Qt |

Size of the ARMSX2 core integration: ~340 lines common, ~190 Vulkan, ~212 OpenGL, ~123 Metal,
~390 CMake, ~250 lines of bridge code for parameter enumeration.

## 4. librashader facts that shape the design

- Latest: **v0.12.0 (2026-07-04)**, C ABI 2, API 5, stable Rust, MSRV 1.88. Library licence
  `MPL-2.0 OR GPL-3.0-only`; headers `librashader.h` and `librashader_ld.h` are **MIT**.
  Compatible with PCSX2's GPL-3 either way.
- Prebuilt release zips: Windows x64, Windows ARM64, Windows x64 win7, macOS arm64, and a
  "x86_64-macos" zip that is **actually arm64** (CI bug). Intel macOS must be self-built.
  Windows DLL hard-imports **`D3DX9_43.dll`** (legacy DirectX runtime, from the default
  `runtime-d3d9` feature) and `d3dcompiler_47.dll`; delay-loads `dxcompiler.dll`. The macOS
  dylib has an absolute CI install name and no rpath. Conclusion: **the stock prebuilts are not
  shippable as-is**; a slim custom build (no d3d9) is required for Windows, and an
  `install_name_tool` fix plus re-sign for macOS.
- Build from source needs only Rust stable and a C++ compiler (vendored glslang and
  SPIRV-Cross via the `cc` crate). Command:
  `cargo build -p librashader-capi --profile optimized --no-default-features --features runtime-opengl,runtime-vulkan,runtime-d3d11,runtime-d3d12` (Windows) or `runtime-vulkan,runtime-metal` (macOS); rename `librashader_capi.*` to `librashader.*`.
- Consumption: recommended path is runtime loading (`librashader_ld.h` pattern with an ABI
  check via `libra_abi_version`). Its loader uses bare library names, so PCSX2 should use its
  own `common/DynamicLibrary` with an explicit path (app dir on Windows, `Frameworks` on macOS)
  and fill the same function-pointer table. Static linking is "not officially supported".
- Backend requirements: GL 3.3+ (DSA optional, enables cache); Vulkan needs `entry`
  (`vkGetInstanceProcAddr`) and, for dynamic rendering, resolves **core 1.3
  `vkCmdBeginRendering`**, otherwise silently falls back to render passes with one framebuffer
  per pass per frame; D3D11 FL 11_0 via `d3dcompiler_47`; D3D12 needs SM 6.0 and
  `dxcompiler.dll` (delay-loaded; static variant is x64-only), uses D3D12 render passes;
  Metal compiles MSL at runtime with `newLibraryWithSource`, min macOS 11.0, not thread safe.
- Per-frame contract: caller supplies source (shader-read state), output (render-target state),
  viewport, monotonically increasing frame count. librashader records into the caller's
  command buffer or list and **never submits**; no barrier after the final pass. `_create`
  (non-deferred) submits and waits internally for LUT uploads; `_create_deferred` exists for
  VK/D3D11/D3D12/Metal. `_frame` needs external synchronisation; `set_param` is thread-safe.
- Presets: absolute preset path, `#reference` and `#include` relative to the containing file,
  paths are `canonicalize()`d so every referenced file must exist. Parser is stricter than
  RetroArch's. LUT formats png, tga, jpeg, bmp.
- Shader cache: persy DB at `%LOCALAPPDATA%\librashader` or `~/Library/Caches/librashader`,
  not redirectable; per-chain `disable_cache`; Metal has no cache. All passes compile at
  chain creation (an 18-pass NTSC preset is a visible hitch on the GS thread).

## 5. Required actions

### 5.1 Dependency acquisition and build (Windows + macOS)

Decision to make: **(a) build librashader-capi in the deps scripts** or **(b) download the
upstream prebuilt zips and patch them**. Recommendation: (a), because (b) cannot cover Intel
macOS, ships an unneeded D3D9 dependency that may fail `LoadLibrary` on clean Windows, and
needs install-name surgery anyway. Both variants land in `deps/` next to shaderc, which the
build already treats as a prebuilt shared library.

Actions:
1. `scripts/windows/build-dependencies.bat` and `-arm64.bat`: install/verify Rust
   (`rustup toolchain install 1.88`, `rustup target add aarch64-pc-windows-msvc`), clone the
   librashader tag, run the slim cargo build, copy `librashader.dll` + `.dll.lib` + header
   into `deps/{bin,lib,include}`. Add `librashader.dll` to `<DepsDLLs>` in
   `common/vsprops/LinkPCSX2Deps.props`. Add `dxcompiler.dll` to the DLL list for D3D12.
   Bump the deps cache key (script hash does this automatically).
2. `scripts/macos/build-dependencies.sh` and `-universal.sh`: same, building
   `runtime-vulkan,runtime-metal` for the host arch (and cross for x86_64 via
   `--target x86_64-apple-darwin` in the universal script), fixing
   `install_name_tool -id @rpath/librashader.dylib`, installing to `$INSTALLDIR/lib`.
3. CMake: `cmake/FindLibrashader.cmake` modelled on `FindShaderc.cmake`; option
   `USE_LIBRASHADER` in `BuildParameters.cmake`; add the dylib as a `Frameworks` bundle
   resource in `setup_main_executable()`; define `PCSX2_HAS_LIBRASHADER` on `PCSX2_FLAGS`
   (ARMSX2 learned the LTO split target needs the define on the interface target).
4. MSBuild: include path from `$(DepsIncludeDir)` already global; runtime loading means no
   `.lib` link is strictly needed, only the DLL copy. Add new source files to `pcsx2.vcxproj`
   and `.filters`.
5. Runtime loader in core (`pcsx2/GS/ShaderChain/LibrashaderLoader.{h,cpp}`): open
   `librashader.dll` / `Frameworks/librashader.dylib` via `DynamicLibrary`, check
   `libra_abi_version() == LIBRASHADER_CURRENT_ABI`, resolve the ~30 functions used, expose
   `IsAvailable()`. Feature degrades to "unavailable" with an OSD/log message if missing.
6. Vendor `include/librashader.h` (MIT) under `3rdparty/librashader/include` with its licence.
7. Linux (out of scope but CI-visible): AppImage deps script can build it the same way;
   flatpak needs `org.freedesktop.Sdk.Extension.rust-stable`. Alternatively guard with
   `USE_LIBRASHADER=OFF` on Linux initially.

Effort: 3 to 5 days (cross-compiles and CI cache debugging dominate).

### 5.2 Core render integration (common code)

Port from ARMSX2 with these upstream-specific adjustments:
1. `Config.h` / `Pcsx2Config.cpp`: `ShaderChainEnabled`, `ShaderChainPreset`, plus (new)
   `ShaderChainSourceMode` (native vs upscaled input) and `ShaderChainParams` string blob or
   per-preset INI section for parameter overrides. Add to `OptionsAreEqual`; no restart.
2. `GSDevice.h/.cpp`: the `DoApplyShaderChain` / `ReleaseShaderChain` virtuals, dedicated
   `m_shader_chain_source/target`, `ApplyShaderChain(output_size, source_size)` with the
   native downscale via `FilteredDownsampleTexture`/`StretchRect`, frame-count ownership,
   parameter store with generation counter, `ClearCurrent` cleanup, `Destroy` cleanup.
3. `GSRenderer.cpp`: call `ApplyShaderChain` in `Merge` after FXAA (:250) and before the
   BilinearSharp resize (:255), using `CalculateDrawDstRect` size as output. **Upstream lacks
   ARMSX2's `MergeMode` composition skip**, so add an early-out: do not run the chain when
   the frame will not be presented (`SkipDuplicateFrames` path / `ShouldSkipPresentingFrame`)
   and bump the frame counter instead. Decide interaction with CAS and TV shaders: recommend
   forcing `CASMode=Disabled` and `TVShader=0` while the chain is active, or documenting that
   they stack.
4. Decision: chain in `Merge` (affects screenshots and video capture, like ShadeBoost) versus
   in `VSync` before `PresentRect` (display-only, like RetroArch's default). ARMSX2 chose
   `Merge`. Either is a few lines; the choice changes user-visible behaviour.
5. Preset load failure: latch per preset, surface via `Host::AddIconOSDMessage` (ARMSX2 only
   logs to console).
6. Optional: create the chain on a worker thread using `_create_deferred` to avoid the
   compile hitch on the GS thread. Defer to a later phase.

Effort: 2 to 3 days.

### 5.3 Backends

| Backend | Status | Work | Risks / notes | Effort |
|---|---|---|---|---|
| Vulkan | Port from ARMSX2 (~190 lines) | `libra_device_vk_t` with `entry = vkGetInstanceProcAddr` from `VKLoader`; `EndRenderPass`, layout transitions via `GSTextureVK::TransitionToLayout`, frame on `GetCurrentCommandBuffer()`, `OverrideImageLayout` after; `frames_in_flight` must equal `NUM_COMMAND_BUFFERS` (3) | Replace the unconditional `ExecuteCommandBuffer(false)` with `InvalidateCachedState()` + `SetInitialState()` and test on AMD/NVIDIA/Intel/MoltenVK. Dynamic rendering needs a **Vulkan 1.3 device** (librashader resolves the core function, not KHR); PCSX2 creates 1.1, so either bump `apiVersion` when the driver supports 1.3 or accept the render-pass fallback. Test MoltenVK on macOS | 2 to 3 days |
| OpenGL (Windows only) | Port (~212 lines) | `libra_gl_loader_t` via `GLContext::GetProcAddress`, `use_dsa=true`, `glsl_version=0`, sized formats from `GetGLFormat()`; `RestoreGLStateAfterShaderChain()` re-pushing `GLState` (fbo, vao, viewport, colour mask, texture units, samplers, program) | Immediate execution, no ring issues | 1 to 2 days |
| Metal (macOS) | Port (~123 lines) | ObjC++ TU (`__OBJC__` required by header), `EndRenderPass`, frame on `GetRenderCmdBuf()`, `FlushEncoders()` after | Not thread safe; no shader cache, so first load of a large preset is slow every launch | 1 to 2 days |
| D3D11 (Windows) | **New** | `libra_d3d11_filter_chain_create(&preset, m_dev, &opts, &chain)`; frame with `ctx = nullptr` (immediate), `GSTexture11` exposes SRV/RTV operators; librashader saves and restores D3D11 state itself | PCSX2 `m_state` cache has no invalidate helper; verify what librashader's restore covers and patch `m_state` for anything it does not (same pattern as `BeginPresent` :1096-1128). Needs `d3dcompiler_47.dll` (inbox) | 1.5 to 2 days |
| D3D12 (Windows) | **New** | `libra_d3d12_filter_chain_create(&preset, m_device, &opts{frames_in_flight=3}, &chain)`; `EndRenderPass()` first; `GSTexture12::TransitionToState` to `PIXEL_SHADER_RESOURCE` / `RENDER_TARGET`; frame with `libra_image_d3d12_t{RESOURCE, ...}` on `GetCommandList()`; then `InvalidateCachedState()` | librashader sets its own descriptor heaps; PCSX2 calls `SetDescriptorHeaps` **only at command-list reset** ([GSDevice12.cpp:525](pcsx2/GS/Renderers/DX12/GSDevice12.cpp#L525)), so heaps must be re-bound after the chain. Ship `dxcompiler.dll` (delay-loaded) in deps; ARM64 cannot use the static DXC. Keep resources alive until the list executes | 2 to 3 days |

Subtotal: 8 to 12 days.

### 5.4 Preset and parameter API, hotkeys, OSD

1. `pcsx2/GS/ShaderChain/ShaderPresets.{h,cpp}`: enumerate `*.slangp` recursively under
   `EmuFolders::Shaders` (skip `__MACOSX`, `.DS_Store`), return display names relative to the
   root; `GetPresetParameters(path)` wrapping `libra_preset_create_with_options` +
   `libra_preset_get_runtime_params` (own handle, freed after); usable from the UI thread.
2. Parameter overrides: persist per preset (INI section `ShaderChainParams/<preset-hash>` or a
   JSON blob as ARMSX2 did); push through `GSDevice::SetShaderChainParams`; "reset" writes the
   preset's `initial` values (librashader has no unset).
3. Hotkeys in `g_gs_hotkeys`: `ToggleShaderChain`, `NextShaderPreset`, `PreviousShaderPreset`
   following `CycleTVShader`; OSD message via `Host::AddKeyedOSDMessage`.
4. Settings overlay (`ImGuiManager::DrawSettingsOverlay`): show active preset name.
5. Wildcard context (`libra_preset_ctx_set_runtime`, core name "PCSX2") so presets with
   `$VID-DRV$` references resolve.

Effort: 2 to 3 days.

### 5.5 Qt settings UI

1. `GraphicsPostProcessingSettingsTab.ui`: new group "Shader Chain (librashader)" with enable
   checkbox, preset combobox (populated from disk, "(None)" first, per-game "Use Global"
   handled by `SettingWidgetBinder` string binding), Refresh, Open Folder, "Download Shader
   Packs..." button, "Parameters..." button, source mode combobox. Disable the group with a
   tooltip when the loader reports librashader unavailable.
2. `ShaderParametersDialog.{h,cpp,ui}`: table of sliders/spinboxes generated from
   `GetPresetParameters`, min/max/step from the preset, live apply (parameter store is
   thread-safe), Reset All, per-game aware.
3. `FolderSettingsWidget`: add the Shaders folder row.
4. `registerWidgetHelp` texts and translation strings; update `pcsx2-qt.vcxproj`/`.filters`
   and `pcsx2-qt/CMakeLists.txt`.

Effort: 4 to 6 days.

### 5.6 Shader pack downloader

Sources (verified 2026-09-12):

| Pack | URL | Size | Layout | Install target |
|---|---|---|---|---|
| libretro slang shaders | `https://buildbot.libretro.com/assets/frontend/shaders_slang.zip` | 54.3 MB zip, 71.4 MB / 5,731 files extracted | **No top-level folder**; category dirs at root | `<Shaders>/shaders_slang/` (create it; RetroArch does the same) |
| Retro Crisis GDV-NTSC | GitHub release asset, latest tag `20260820`: `.../releases/download/20260820/Retro.Crisis.GDV-NTSC.2026.08.20.zip.zip` | 328 KB, 333 `.slangp` | Top-level `retro crisis/` with 7 resolution folders, `__MACOSX` junk, `.DS_Store`; presets `#reference` each other and use `../../../shaders_slang/crt/shaders/guest/advanced/...` | **Must** land at `<Shaders>/shaders_slang/retro crisis/<res>/...`; requires shaders_slang installed first. Repo tree contains only README + LICENSE (GPL-3.0); assets only in releases |
| satpixie CRT | `https://github.com/Conkwer/satpixie-crt-shader/releases/download/20260122/satpixie-crt-shader-20260122.zip` | 48 KB | Top-level `satpixie-crt-shader/RetroArch/shaders/shaders_slang/crt/{satpixie-crt.slangp, shaders/satpixie/*.slang}`; self-contained (no includes, no LUTs) | Strip prefix, place at `<Shaders>/shaders_slang/crt/` |

Facts affecting design: buildbot has no manifest (`.index` returns 404); use `ETag` /
`Last-Modified` from HEAD for update checks. Buildbot supports range requests; GitHub release
downloads redirect and codeload archives send no `Content-Length`. RetroCrisis release notes
say to delete old packs before installing. Every pack has different strip-prefix and
destination rules, so a small static table in code (id, name, URL or "latest GitHub release"
resolver, strip components, destination subdir, licence note, depends-on) is sufficient; no
remote manifest is needed for three sources.

Actions:
1. `EmuFolders::Shaders` (default `DataRoot/shaders`) registered in the four places listed in
   2.4.
2. `pcsx2/ShaderPacks.{h,cpp}` (core, because libzip links there): pack table; `Download(pack,
   ProgressCallback*)` using `HTTPDownloader` with per-request progress; `ExtractZipToDirectory
   (data, dest, strip_components, ProgressCallback*)` using `zip_open_buffer_managed` and
   `zip_stat_index`/`zip_fopen_index`, creating directories, skipping `__MACOSX` and
   `.DS_Store`, rejecting absolute paths and `..` segments (zip-slip), writing with
   `FileSystem::WriteBinaryFile`; install marker file with ETag/tag for "installed / update
   available" status; dependency check (RetroCrisis requires shaders_slang).
3. Memory: `HTTPDownloader` buffers the whole body; 54 MB is acceptable but consider adding a
   stream-to-file mode to `HTTPDownloader` if larger packs are added later. Not required now.
4. `pcsx2-qt/ShaderPackDownloadDialog.{h,cpp,ui}` modelled on `CoverDownloadDialog`: pack list
   with checkboxes and status, licence summary, progress bar, cancel, uses a
   `QtAsyncProgressThread` subclass; on completion signals the settings tab to refresh the
   preset list. Reachable from the Post-Processing tab and optionally the Tools menu.
5. GitHub "latest release" resolution for RetroCrisis and satpixie: either pin the asset URL
   (simplest, needs periodic bumps) or call `api.github.com/repos/<r>/releases/latest` (rate
   limited to 60/hour unauthenticated; fine for a manual action). Recommend pinned URL plus a
   pinned tag shown in the UI, upgradeable later.

Effort: 4 to 6 days.

### 5.7 Licensing and documentation
- Add librashader (MPL-2.0, dynamically loaded) and the MIT header to the third-party licence
  document used by the About dialog; note that shader packs are downloaded by the user and
  carry their own licences (slang-shaders per-file, RetroCrisis GPL-3.0, satpixie per repo).
- Short user doc in the settings help text on where the shaders folder is and how presets
  reference each other.

Effort: 0.5 day.

### 5.8 Tests and verification
- Unit tests (`tests/ctest/core`): zip extraction (nested dirs, strip components, zip-slip
  rejection, junk filtering), pack table integrity, preset enumeration on a fixture tree.
- No harness exists for `GSDevice`; backend work is verified manually. Matrix: 5 backends on
  Windows (D3D11, D3D12, Vulkan, OpenGL) and macOS (Metal, Vulkan/MoltenVK), each with a
  1-pass preset, the 18-pass RetroCrisis preset, satpixie (uses `PassFeedback`), a preset
  with LUTs, preset switching at runtime, renderer switching with chain active, window resize,
  frame skipping / SkipDuplicateFrames, screenshot and video capture, per-game override.
- CI: deps cache rebuild on Windows x64 and macOS both arches; confirm `unittests` green.

Effort: 1 to 2 days automated, 2 to 3 days manual QA.

## 6. Design decisions to settle before implementation
1. Build librashader in deps scripts (recommended) vs patched upstream prebuilts.
2. Runtime `DynamicLibrary` loading (recommended) vs import-lib linking.
3. Chain in `Merge` (captured in screenshots/video) vs before `PresentRect` (display only).
4. Behaviour of CAS and TV shaders while the chain is active.
5. Feed native PCRTC resolution (ARMSX2 default, correct for CRT shaders) vs upscaled, or
   expose both.
6. Bump Vulkan device to 1.3 when available (dynamic rendering) vs accept fallback.
7. Whether Linux is in scope for the first PR (affects flatpak manifest).
8. Whether this targets upstream PCSX2 or a fork: upstream maintainers may object to a Rust
   toolchain in the deps pipeline and to an MPL runtime dependency; a prebuilt-only path or
   opt-in `USE_LIBRASHADER` reduces friction.

## 7. Effort summary

| Workstream | Days (low) | Days (high) |
|---|---|---|
| 5.1 Dependency and build (Win + macOS, CI) | 3 | 5 |
| 5.2 Core integration | 2 | 3 |
| 5.3 Backends (VK, GL, Metal port + D3D11, D3D12 new) | 8 | 12 |
| 5.4 Preset/param API, hotkeys, OSD | 2 | 3 |
| 5.5 Qt UI | 4 | 6 |
| 5.6 Downloader | 4 | 6 |
| 5.7 Licensing/docs | 0.5 | 0.5 |
| 5.8 Tests + manual QA | 3 | 5 |
| **Total** | **26.5** | **40.5** |

MVP cut (12 to 15 days): 5.1 without ARM64, 5.2, Vulkan + Metal + D3D11, preset picker only
(no parameter editor), downloader for shaders_slang only, no hotkeys.

## 8. Suggested phasing
1. Build and loader: librashader in deps, `FindLibrashader.cmake`, runtime loader, ABI check.
   Verify: PCSX2 starts with and without the library present on both OSes.
2. Core + Vulkan + Metal (ports). Verify: a 1-pass preset renders on Windows/Vulkan and
   macOS/Metal; frame counter advances on skipped frames.
3. D3D11, then D3D12 (descriptor heap re-bind). Verify: same presets on both.
4. OpenGL (Windows). Verify: `GLState` consistent after chain (no corrupted UI/present).
5. Config, hotkeys, Qt preset picker, Shaders folder. Verify: per-game override works.
6. Downloader + extraction helper + unit tests. Verify: RetroCrisis preset loads after
   installing shaders_slang and the pack, fails with a clear message without shaders_slang.
7. Parameter editor and persistence.
8. Licences, docs, CI matrix, manual QA sweep.

## 9. Sources
- ARMSX2 shader chain code: `pcsx2/GS/Renderers/Common/GSDevice.{h,cpp}`, `GSRenderer.cpp`,
  `Vulkan/GSDeviceVK.cpp:4755-4944`, `OpenGL/GSDeviceOGL.cpp:2848-3059`,
  `Metal/GSDeviceMTL.mm:790-912`, `platforms/*/3rdparty/librashader/CMakeLists.txt`,
  `docs/superpowers/plans/2026-09-12-shader-chain-perf-and-driver-source.md`.
- librashader: https://github.com/SnowflakePowered/librashader (releases, `include/*.h`,
  `librashader-capi/Cargo.toml`, README licence section).
- Shader packs: https://buildbot.libretro.com/assets/frontend/ ,
  https://github.com/libretro/slang-shaders , https://github.com/RetroCrisis/Retro-Crisis-GDV-NTSC/releases ,
  https://github.com/Conkwer/satpixie-crt-shader/releases .
