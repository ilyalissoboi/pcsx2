# librashader Shader Chain (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let PCSX2 apply a RetroArch `.slangp` post-processing preset to the displayed frame via librashader on Vulkan, Metal, D3D11 and D3D12, selectable from the Qt Post-Processing tab, hot-applied, and gracefully absent when the library is missing.

**Architecture:** librashader 0.12.0 is built by the existing deps scripts and loaded at runtime through `common/DynamicLibrary` with an ABI check. Backend-agnostic code (loader, preset enumeration, parameter store) lives in a new `pcsx2/GS/ShaderChain/` module. `GSDevice` gains `DoApplyShaderChain`/`ReleaseShaderChain` virtuals with no-op defaults; the four backends override them with GPU glue. `GSRenderer::VSync` runs the chain after the skip decision and before the present blit, replacing CAS and the TV shader while active, so screenshots, captures and the OSD stay unshaded.

**Tech Stack:** C++20, Objective-C++ (Metal), CMake + MSBuild, Rust stable 1.88 (deps build only), librashader-capi 0.12.0 (C ABI 2, API 5), Qt 6 widgets, gtest.

**Spec:** `docs/superpowers/specs/2026-09-12-librashader-shader-chain-design.md`

## Global Constraints

- librashader tag `librashader-v0.12.0`; `LIBRASHADER_CURRENT_ABI` must equal 2; `LIBRASHADER_CURRENT_VERSION` is 5.
- Rust toolchain pinned to 1.88 in the deps scripts, invoked as `rustup run 1.88 cargo ...` (the `cargo` proxy symlinks created by rustup 1.29 on Windows do not execute in SSH sessions). No Rust in the PCSX2 build itself.
- Library is loaded at runtime only; never link an import library. File names `librashader.dll` (Windows, next to the exe) and `librashader.dylib` (macOS, `Contents/Frameworks`).
- Cargo features: Windows `runtime-vulkan,runtime-d3d11,runtime-d3d12`; macOS `runtime-vulkan,runtime-metal`. Never enable `runtime-d3d9`.
- Build targets in scope: Windows x64 and macOS arm64 only. Other targets must still compile and run with the feature reporting unavailable.
- INI keys: `EmuCore/GS/ShaderChainEnabled` (bool, default false), `EmuCore/GS/ShaderChainPreset` (string, default empty, relative to Shaders folder, forward slashes), `Folders/Shaders` (default `shaders`).
- Chain input is always native PCRTC resolution; output is the aspect-corrected draw rect size; placement is display-only in `GSRenderer::VSync`.
- Vulkan: `frames_in_flight = NUM_COMMAND_BUFFERS` (3), `use_dynamic_rendering = false`. D3D12: `frames_in_flight = 3`.
- All new files carry the repo SPDX header: `// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team` / `// SPDX-License-Identifier: GPL-3.0+`.
- Logging uses `ERROR_LOG`/`WARNING_LOG`/`INFO_LOG` from `common/Console.h` (fmt style). OSD uses `Host::AddIconOSDMessage(key, ICON_FA_TRIANGLE_EXCLAMATION, msg, Host::OSD_ERROR_DURATION)`.
- Commit after every task with the message shown; end each commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Windows-only tasks (9, 10, 11) are built and verified on the Windows machine `pcsx2-win` over SSH; see "Windows Remote Workflow" below. Edits are made on the Mac and transferred as a git bundle.

## Windows Remote Workflow

The Windows x64 machine (VEGA, 192.168.68.64, user `ilya`, RTX 4090, Windows 11, Visual Studio 2026 18.10 with MSVC 14.51 and clang-cl 22.1, Windows SDK 10.0.26100, rustup 1.29) is reached from the Mac with the SSH alias `pcsx2-win` defined in `~/.ssh/config`. The remote shell is `cmd.exe`. The repo is cloned at `E:\work\pcsx2`; deps land in `E:\work\pcsx2\deps`. The user is logged in on the console, so interactive launches work through scheduled tasks.

**Transfer the branch (no GitHub round-trip):**
```bash
cd ~/work/pcsx2 && git bundle create /tmp/sc.bundle master..feature/librashader-shader-chain && scp /tmp/sc.bundle pcsx2-win:E:/work/sc.bundle
ssh pcsx2-win 'git -C E:\work\pcsx2 fetch E:\work\sc.bundle feature/librashader-shader-chain:feature/librashader-shader-chain && git -C E:\work\pcsx2 checkout feature/librashader-shader-chain'
```
For subsequent updates use `git bundle create /tmp/sc.bundle <last-transferred-sha>..feature/librashader-shader-chain` and `git -C E:\work\pcsx2 fetch E:\work\sc.bundle feature/librashader-shader-chain:feature/librashader-shader-chain` followed by `git -C E:\work\pcsx2 reset --hard feature/librashader-shader-chain` on the checked-out branch.

**Build (always inside a fresh `cmd /c` with vcvars64, because `%VAR%` in a one-liner expands before vcvars runs):**
```bash
ssh pcsx2-win 'cmd /c ""C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d E:\work\pcsx2 && msbuild PCSX2_qt.slnx -m -p:Configuration="Release Clang" -p:Platform=x64 -v:m"'
ssh pcsx2-win 'cmd /c ""C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d E:\work\pcsx2 && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=E:\work\pcsx2\deps -DQT_BUILD=ON -DDISABLE_ADVANCE_SIMD=ON && cmake --build build --target unittests"'
```
Wrapper rule: set `VSCMD_SKIP_SENDTELEMETRY=1` before calling `vcvars64.bat` and redirect output **once** at the outer level (`call inner.cmd > log 2>&1`), never per line after vcvars. vcvars otherwise spawns a telemetry process that inherits the log handle and every later `>>` fails with "file in use", silently skipping the command. Long jobs (the deps script) are started as a scheduled task so they survive the SSH session: a wrapper `E:\work\run-deps.cmd` sets `DEBUG=0`, calls `build-dependencies.bat < nul` (so the `pause` in its `:error` label cannot block) and redirects output to `E:\work\pcsx2-deps-build.log`; `schtasks /Create /TN pcsx2-deps /TR E:\work\run-deps.cmd /SC ONCE /ST 00:00 /F && schtasks /Run /TN pcsx2-deps`. Poll with `ssh pcsx2-win 'powershell -NoProfile -Command "Get-Content E:\work\pcsx2-deps-build.log -Tail 5"'`.

**Run PCSX2 on the Windows desktop (SSH-spawned GUI processes are invisible):**
```bash
ssh pcsx2-win 'schtasks /Create /TN pcsx2-run /TR "E:\work\pcsx2\bin\pcsx2-qt.exe -batch E:\games\test.iso" /SC ONCE /ST 00:00 /RU ilya /IT /F && schtasks /Run /TN pcsx2-run'
ssh pcsx2-win 'taskkill /IM pcsx2-qt.exe'
```
**Screen capture for visual checks (PCSX2 screenshots exclude the chain by design):** a scheduled interactive task runs `powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms,System.Drawing; $b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds; $bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height; $g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size); $bmp.Save('E:\\work\\shot.png')"`; then `scp pcsx2-win:E:/work/shot.png /tmp/shot.png` and inspect the PNG on the Mac.

**Logs:** `scp pcsx2-win:C:/Users/Ilya/Documents/PCSX2/logs/emulog.txt /tmp/emulog-win.txt` (or `E:/work/pcsx2/bin/logs/emulog.txt` in portable mode).

## File Structure

| File | Responsibility |
|---|---|
| `3rdparty/librashader/include/librashader.h`, `LICENSE`, `README.md` | Vendored MIT header for librashader-capi 0.12.0, pinned tag noted |
| `pcsx2/GS/ShaderChain/LibrashaderLoader.{h,cpp}` | Runtime load of the library, ABI check, function-pointer tables, availability query |
| `pcsx2/GS/ShaderChain/ShaderPresets.{h,cpp}` | Enumerate `*.slangp` under `EmuFolders::Shaders`, resolve relative paths, thread-safe parameter store |
| `pcsx2/Config.h`, `pcsx2/Pcsx2Config.cpp` | `ShaderChainEnabled`, `ShaderChainPreset`, `EmuFolders::Shaders` |
| `pcsx2/GS/Renderers/Common/GSDevice.{h,cpp}` | Virtuals, dedicated textures, frame counter, `ApplyShaderChain` |
| `pcsx2/GS/Renderers/Common/GSRenderer.cpp` | `VSync` hook and skipped-frame counter |
| `pcsx2/GS/Renderers/Vulkan/GSDeviceVK.{h,cpp}` | Vulkan override |
| `pcsx2/GS/Renderers/Metal/GSDeviceMTL.{h,mm}` | Metal override |
| `pcsx2/GS/Renderers/DX11/GSDevice11.{h,cpp}` | D3D11 override |
| `pcsx2/GS/Renderers/DX12/GSDevice12.{h,cpp}` | D3D12 override |
| `.github/workflows/scripts/macos/build-dependencies.sh`, `.github/workflows/scripts/windows/build-dependencies.bat` | Build librashader into deps |
| `cmake/FindLibrashader.cmake`, `cmake/BuildParameters.cmake`, `cmake/SearchForStuff.cmake`, `pcsx2/CMakeLists.txt` | Discovery, define, bundling |
| `common/vsprops/LinkPCSX2Deps.props`, `common/vsprops/common.props`, `pcsx2/pcsx2.vcxproj(.filters)` | MSBuild DLL copy, define, sources |
| `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui`, `GraphicsSettingsWidget.{h,cpp}` | Shader Chain group box |
| `pcsx2-qt/Settings/FolderSettingsWidget.{ui,cpp}` | Shaders folder row |
| `tests/ctest/core/shader_presets_tests.cpp`, `librashader_loader_tests.cpp`, `tests/ctest/core/CMakeLists.txt` | Unit tests |

---

### Task 1: Vendor the librashader C header

**Files:**
- Create: `3rdparty/librashader/include/librashader.h`
- Create: `3rdparty/librashader/LICENSE`
- Create: `3rdparty/librashader/README.md`
- Modify: `pcsx2/CMakeLists.txt` (near line 652, the `target_include_directories(PCSX2_FLAGS INTERFACE ${SHADERC_INCLUDE_DIR})` inside `if(USE_VULKAN)`; add a new unconditional line after the `if(USE_VULKAN)` block ends at line 653)
- Modify: `common/vsprops/common.props:28` (`AdditionalIncludeDirectories`)

**Interfaces:**
- Produces: `#include "librashader.h"` resolvable from any `pcsx2/` source. Macros `LIBRASHADER_CURRENT_ABI` (2) and `LIBRASHADER_CURRENT_VERSION` (5). Types `libra_shader_preset_t`, `libra_preset_ctx_t`, `libra_error_t`, `libra_viewport_t`, `libra_preset_param_list_t`, backend chain types and option structs.

- [ ] **Step 1: Fetch the header at the pinned tag**

```bash
cd /tmp && rm -rf librashader-hdr && mkdir librashader-hdr && cd librashader-hdr
curl -fsSL -o librashader.h https://raw.githubusercontent.com/SnowflakePowered/librashader/librashader-v0.12.0/include/librashader.h
grep -n "LIBRASHADER_CURRENT_ABI\|LIBRASHADER_CURRENT_VERSION\|SPDX-License-Identifier" librashader.h | head
```
Expected: `#define LIBRASHADER_CURRENT_VERSION 5`, `#define LIBRASHADER_CURRENT_ABI 2`, `SPDX-License-Identifier: MIT`. If the ABI or version differ, stop and report; the spec is pinned to these values.

- [ ] **Step 2: Vendor the files**

```bash
cd ~/work/pcsx2
mkdir -p 3rdparty/librashader/include
cp /tmp/librashader-hdr/librashader.h 3rdparty/librashader/include/librashader.h
cat > 3rdparty/librashader/LICENSE <<'LIC'
MIT License

Copyright 2022 chyyran

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
LIC
cat > 3rdparty/librashader/README.md <<'RM'
# librashader C header

`include/librashader.h` is the C API header of [librashader](https://github.com/SnowflakePowered/librashader)
(`librashader-capi`), copied verbatim from tag `librashader-v0.12.0` (C ABI 2, API 5).
The header is MIT licensed (see `LICENSE`). The library itself (`librashader.dll` /
`librashader.dylib`, MPL-2.0) is not vendored: it is built by the deps scripts under
`.github/workflows/scripts/{windows,macos}/` and loaded at runtime by
`pcsx2/GS/ShaderChain/LibrashaderLoader.cpp`.

To update: bump the tag here, in both deps scripts, and re-check `LIBRASHADER_CURRENT_ABI`
against `LibrashaderLoader.cpp`.
RM
```

- [ ] **Step 3: Add the include directory to CMake and MSBuild**

In `pcsx2/CMakeLists.txt`, immediately after the closing `endif()` of the `if(USE_VULKAN)` block (line 653), add:

```cmake
# librashader C header (library is loaded at runtime, see GS/ShaderChain/LibrashaderLoader.cpp)
target_include_directories(PCSX2_FLAGS INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/librashader/include)
```

In `common/vsprops/common.props` line 28, change:
```xml
<AdditionalIncludeDirectories>$(SolutionDir);$(ProjectDir);%(AdditionalIncludeDirectories);$(DepsIncludeDir)</AdditionalIncludeDirectories>
```
to:
```xml
<AdditionalIncludeDirectories>$(SolutionDir);$(ProjectDir);$(SolutionDir)3rdparty\librashader\include;%(AdditionalIncludeDirectories);$(DepsIncludeDir)</AdditionalIncludeDirectories>
```

- [ ] **Step 4: Verify the header compiles standalone on macOS**

```bash
cd ~/work/pcsx2 && printf '#include "librashader.h"\nint main(){return LIBRASHADER_CURRENT_ABI==2?0:1;}\n' > /tmp/hdr_test.c && clang -I3rdparty/librashader/include -o /tmp/hdr_test /tmp/hdr_test.c && /tmp/hdr_test && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Commit**

```bash
git add 3rdparty/librashader pcsx2/CMakeLists.txt common/vsprops/common.props
git commit -m "3rdparty: Vendor librashader 0.12.0 C header

Header only (MIT). The library is built by the deps scripts and loaded at
runtime.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Config keys and Shaders folder

**Files:**
- Modify: `pcsx2/Config.h:817` (bitfield), `:939` (strings), `:1474` (EmuFolders externs)
- Modify: `pcsx2/Pcsx2Config.cpp:172` (EmuFolders definitions), `:820-904` (`OptionsAreEqual`), `:1016` and `:1119` (`LoadSave`), `:2296-2311` (`SetDefaults`), `:2322-2338` (`LoadConfig`), `:2360-2380` (`EnsureFoldersExist`)

**Interfaces:**
- Produces: `GSConfig.ShaderChainEnabled` (bool bit), `GSConfig.ShaderChainPreset` (std::string, relative, `/` separators), `EmuFolders::Shaders` (std::string absolute path).

This task is mechanical config plumbing with no unit-test harness for `Pcsx2Config`; verification is a build plus an INI round-trip.

- [ ] **Step 1: Add the bitfield and string fields**

In `pcsx2/Config.h`, after line 817 (`ShadeBoost : 1,`) insert:
```cpp
					ShaderChainEnabled : 1,
```
After line 939 (`std::string Adapter;`) insert:
```cpp
		/// Path of the active librashader .slangp preset, relative to EmuFolders::Shaders. Empty = none.
		std::string ShaderChainPreset;
```
In the `EmuFolders` namespace after `extern std::string DebuggerSettings;` (line 1474) insert:
```cpp
	extern std::string Shaders;
```

- [ ] **Step 2: Add definitions, equality, and serialization**

In `pcsx2/Pcsx2Config.cpp` after `std::string Videos;` (line 172) insert:
```cpp
	std::string Shaders;
```
In `OptionsAreEqual`, after `OpEqu(Adapter) &&` (line 900) insert:
```cpp
		OpEqu(ShaderChainPreset) &&
```
(`ShaderChainEnabled` is covered by `OpEqu(bitsets[0])`/`bitsets[1]`.)

In `LoadSave`, after `SettingsWrapBitBool(ShadeBoost);` (line 1016) insert:
```cpp
	SettingsWrapBitBool(ShaderChainEnabled);
```
After `SettingsWrapEntry(Adapter);` (line 1119) insert:
```cpp
	SettingsWrapEntry(ShaderChainPreset);
```

In `EmuFolders::SetDefaults` add after the `Videos` line:
```cpp
	si.SetStringValue("Folders", "Shaders", "shaders");
```
In `EmuFolders::LoadConfig` add after the `Videos` line:
```cpp
	Shaders = LoadPathFromSettings(si, DataRoot, "Shaders", "shaders");
```
In `EmuFolders::EnsureFoldersExist` add before `return result;`:
```cpp
	result = FileSystem::CreateDirectoryPath(Shaders.c_str(), false) && result;
```

- [ ] **Step 3: Build the core**

```bash
cd ~/work/pcsx2 && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Devel -DCMAKE_PREFIX_PATH="$HOME/deps" -DCMAKE_OSX_ARCHITECTURES=arm64 -DDISABLE_ADVANCE_SIMD=ON >/dev/null && cmake --build build --target PCSX2 2>&1 | tail -3
```
Expected: build succeeds (`ninja: no work to do` or link of `libPCSX2.a`). If `$HOME/deps` does not exist yet, run `.github/workflows/scripts/macos/build-dependencies-universal.sh ~/deps` first (over two hours; it is a one-off; needs Homebrew `nasm` and full Xcode).

- [ ] **Step 4: INI round-trip check**

```bash
cd ~/work/pcsx2 && cmake --build build --target pcsx2-qt 2>&1 | tail -1 && open build/bin/PCSX2.app && sleep 8 && osascript -e 'quit app "PCSX2"' ; grep -n "ShaderChain\|Shaders" ~/Library/Application\ Support/PCSX2/inis/PCSX2.ini
```
Expected: lines `ShaderChainEnabled = false`, `ShaderChainPreset =` under `[EmuCore/GS]` and `Shaders = shaders` under `[Folders]`; directory `~/Library/Application Support/PCSX2/shaders` exists (`ls -d`). If the machine uses portable mode, check `bin/inis/PCSX2.ini` instead.

- [ ] **Step 5: Commit**

```bash
git add pcsx2/Config.h pcsx2/Pcsx2Config.cpp
git commit -m "Config: Add ShaderChainEnabled/ShaderChainPreset and Shaders folder

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: ShaderPresets module (enumeration, path resolution, parameter store)

**Files:**
- Create: `pcsx2/GS/ShaderChain/ShaderPresets.h`, `pcsx2/GS/ShaderChain/ShaderPresets.cpp`
- Create: `tests/ctest/core/shader_presets_tests.cpp`
- Modify: `tests/ctest/core/CMakeLists.txt:1-5`, `pcsx2/CMakeLists.txt:502` (source list), `pcsx2/pcsx2.vcxproj:338`, `pcsx2/pcsx2.vcxproj.filters:1079`

**Interfaces:**
- Consumes: `EmuFolders::Shaders` (Task 2), `FileSystem::FindFiles`, `Path::Combine`, `Path::IsAbsolute`.
- Produces:
```cpp
namespace ShaderPresets {
	/// Relative paths ('/' separators) of all *.slangp under EmuFolders::Shaders, sorted, excluding
	/// anything under a "__MACOSX" directory or whose file name starts with '.'.
	std::vector<std::string> Enumerate();
	/// Same as Enumerate() but against an explicit root (used by tests).
	std::vector<std::string> EnumerateIn(const std::string& root);
	/// Absolute path for a relative preset, or empty if the input is empty, absolute, or escapes the root.
	std::string ResolvePresetPath(std::string_view relative);
	std::string ResolvePresetPathIn(const std::string& root, std::string_view relative);

	struct ParameterStore {
		void Set(std::string preset, std::vector<std::pair<std::string, float>> params); // UI thread
		u64 GetGeneration() const;                                                        // lock-free
		/// Copies the current preset/params into out; returns the generation observed.
		u64 Snapshot(std::string* preset, std::vector<std::pair<std::string, float>>* params) const;
	};
	ParameterStore& Params();
}
```

- [ ] **Step 1: Write the failing tests**

Create `tests/ctest/core/shader_presets_tests.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderPresets.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>

namespace
{
	class TempTree
	{
	public:
		TempTree()
		{
			for (int i = 0; i < 1000 && m_root.empty(); i++)
			{
				std::string candidate = Path::Combine(std::filesystem::temp_directory_path().string(),
					"pcsx2_shader_presets_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempTree() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }
		void file(const char* rel)
		{
			const std::string full = Path::Combine(m_root, rel);
			FileSystem::CreateDirectoryPath(std::string(Path::GetDirectory(full)).c_str(), true);
			FileSystem::WriteStringToFile(full.c_str(), "#reference nothing\n");
		}

	private:
		std::string m_root;
	};
} // namespace

TEST(ShaderPresets, EnumerateFindsNestedSlangpSortedWithForwardSlashes)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("shaders_slang/crt/crt-royale.slangp");
	t.file("shaders_slang/crt/shaders/royale/first.slang"); // not a preset
	t.file("shaders_slang/anti-aliasing/aa.slangp");
	t.file("top.slangp");

	const std::vector<std::string> result = ShaderPresets::EnumerateIn(t.root());
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "shaders_slang/anti-aliasing/aa.slangp");
	EXPECT_EQ(result[1], "shaders_slang/crt/crt-royale.slangp");
	EXPECT_EQ(result[2], "top.slangp");
}

TEST(ShaderPresets, EnumerateSkipsMacJunkAndDotFiles)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("shaders_slang/crt/good.slangp");
	t.file("shaders_slang/__MACOSX/crt/._good.slangp");
	t.file("shaders_slang/crt/.hidden.slangp");

	const std::vector<std::string> result = ShaderPresets::EnumerateIn(t.root());
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0], "shaders_slang/crt/good.slangp");
}

TEST(ShaderPresets, EnumerateOnMissingRootIsEmpty)
{
	EXPECT_TRUE(ShaderPresets::EnumerateIn("/definitely/not/here/pcsx2").empty());
}

TEST(ShaderPresets, ResolveRejectsEmptyAbsoluteAndEscaping)
{
	const std::string root = "/root/shaders";
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "/etc/passwd").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "../x.slangp").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "a/../../x.slangp").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "C:/x.slangp").empty());
}

TEST(ShaderPresets, ResolveJoinsRelativeUnderRoot)
{
	const std::string root = "/root/shaders";
	const std::string got = ShaderPresets::ResolvePresetPathIn(root, "shaders_slang/crt/a.slangp");
	EXPECT_EQ(got, Path::Combine(root, "shaders_slang/crt/a.slangp"));
}

TEST(ShaderPresets, ParameterStoreGenerationAndSnapshot)
{
	ShaderPresets::ParameterStore store;
	const u64 g0 = store.GetGeneration();

	std::string preset;
	std::vector<std::pair<std::string, float>> params;
	EXPECT_EQ(store.Snapshot(&preset, &params), g0);
	EXPECT_TRUE(preset.empty());
	EXPECT_TRUE(params.empty());

	store.Set("shaders_slang/crt/a.slangp", {{"gamma", 2.4f}, {"mask", 1.0f}});
	EXPECT_GT(store.GetGeneration(), g0);
	const u64 g1 = store.Snapshot(&preset, &params);
	EXPECT_EQ(g1, store.GetGeneration());
	EXPECT_EQ(preset, "shaders_slang/crt/a.slangp");
	ASSERT_EQ(params.size(), 2u);
	EXPECT_EQ(params[0].first, "gamma");
	EXPECT_FLOAT_EQ(params[0].second, 2.4f);

	store.Set("shaders_slang/crt/a.slangp", {});
	EXPECT_GT(store.GetGeneration(), g1);
}
```

Add `shader_presets_tests.cpp` to `tests/ctest/core/CMakeLists.txt`:
```cmake
add_pcsx2_test(core_test
	patch_tests.cpp
	shader_presets_tests.cpp
	MockMemoryInterface.h
	StubHost.cpp
)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd ~/work/pcsx2 && cmake --build build --target core_test 2>&1 | grep -m1 -i "error" 
```
Expected: a compile error naming `GS/ShaderChain/ShaderPresets.h: No such file`.

- [ ] **Step 3: Implement the module**

Create `pcsx2/GS/ShaderChain/ShaderPresets.h`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Backend-agnostic helpers for librashader presets. No GPU code here.
namespace ShaderPresets
{
	/// Relative paths ('/' separators) of all *.slangp under EmuFolders::Shaders, sorted.
	/// Skips anything under a "__MACOSX" directory and file names starting with '.'.
	std::vector<std::string> Enumerate();
	std::vector<std::string> EnumerateIn(const std::string& root);

	/// Absolute path for a preset stored relative to EmuFolders::Shaders.
	/// Returns empty if relative is empty, absolute, or contains a ".." component.
	std::string ResolvePresetPath(std::string_view relative);
	std::string ResolvePresetPathIn(const std::string& root, std::string_view relative);

	/// Parameter overrides written by the UI thread and drained by the GS thread.
	/// Readers poll GetGeneration() without locking and call Snapshot() only when it changed.
	class ParameterStore
	{
	public:
		using ParamList = std::vector<std::pair<std::string, float>>;

		void Set(std::string preset, ParamList params);
		u64 GetGeneration() const { return m_generation.load(std::memory_order_acquire); }
		u64 Snapshot(std::string* preset, ParamList* params) const;

	private:
		mutable std::mutex m_mutex;
		std::string m_preset;
		ParamList m_params;
		std::atomic<u64> m_generation{0};
	};

	ParameterStore& Params();
} // namespace ShaderPresets
```

Create `pcsx2/GS/ShaderChain/ShaderPresets.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderPresets.h"
#include "Config.h"

#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include <algorithm>

namespace
{
	bool IsJunkPath(std::string_view rel)
	{
		// Split on '/', reject __MACOSX directories and dot-prefixed names anywhere in the path.
		size_t start = 0;
		while (start <= rel.size())
		{
			const size_t end = rel.find('/', start);
			const std::string_view part = rel.substr(start, (end == std::string_view::npos) ? std::string_view::npos : end - start);
			if (part == "__MACOSX" || (!part.empty() && part[0] == '.'))
				return true;
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}
		return false;
	}

	bool HasDotDotComponent(std::string_view rel)
	{
		size_t start = 0;
		while (start <= rel.size())
		{
			const size_t end = rel.find_first_of("/\\", start);
			const std::string_view part = rel.substr(start, (end == std::string_view::npos) ? std::string_view::npos : end - start);
			if (part == "..")
				return true;
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}
		return false;
	}
} // namespace

std::vector<std::string> ShaderPresets::EnumerateIn(const std::string& root)
{
	std::vector<std::string> result;
	if (root.empty() || !FileSystem::DirectoryExists(root.c_str()))
		return result;

	FileSystem::FindResultsArray files;
	FileSystem::FindFiles(root.c_str(), "*.slangp",
		FILESYSTEM_FIND_RECURSIVE | FILESYSTEM_FIND_RELATIVE_PATHS | FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_HIDDEN_FILES,
		&files);

	result.reserve(files.size());
	for (FILESYSTEM_FIND_DATA& fd : files)
	{
		std::string rel = std::move(fd.FileName);
		std::replace(rel.begin(), rel.end(), '\\', '/');
		if (IsJunkPath(rel))
			continue;
		result.push_back(std::move(rel));
	}

	std::sort(result.begin(), result.end());
	return result;
}

std::vector<std::string> ShaderPresets::Enumerate()
{
	return EnumerateIn(EmuFolders::Shaders);
}

std::string ShaderPresets::ResolvePresetPathIn(const std::string& root, std::string_view relative)
{
	if (relative.empty() || root.empty())
		return {};
	// Reject absolute paths on any platform (leading slash, or drive letter).
	if (relative[0] == '/' || relative[0] == '\\' || Path::IsAbsolute(relative) ||
		(relative.size() >= 2 && relative[1] == ':'))
		return {};
	if (HasDotDotComponent(relative))
		return {};
	return Path::Combine(root, relative);
}

std::string ShaderPresets::ResolvePresetPath(std::string_view relative)
{
	return ResolvePresetPathIn(EmuFolders::Shaders, relative);
}

void ShaderPresets::ParameterStore::Set(std::string preset, ParamList params)
{
	{
		std::lock_guard lock(m_mutex);
		m_preset = std::move(preset);
		m_params = std::move(params);
	}
	m_generation.fetch_add(1, std::memory_order_acq_rel);
}

u64 ShaderPresets::ParameterStore::Snapshot(std::string* preset, ParamList* params) const
{
	std::lock_guard lock(m_mutex);
	*preset = m_preset;
	*params = m_params;
	return m_generation.load(std::memory_order_acquire);
}

ShaderPresets::ParameterStore& ShaderPresets::Params()
{
	static ParameterStore s_store;
	return s_store;
}
```

Register the sources. In `pcsx2/CMakeLists.txt` after line 502 (`GS/Renderers/Common/GSDevice.cpp`) add to the same `pcsx2GSSources` list:
```cmake
	GS/ShaderChain/ShaderPresets.cpp
```
and in the corresponding headers list (`pcsx2GSHeaders`, near line 542 `GS/Renderers/Common/GSDevice.h`):
```cmake
	GS/ShaderChain/ShaderPresets.h
```
In `pcsx2/pcsx2.vcxproj` after line 338 add `<ClCompile Include="GS\ShaderChain\ShaderPresets.cpp" />`, and after line 776 add `<ClInclude Include="GS\ShaderChain\ShaderPresets.h" />`. In `pcsx2/pcsx2.vcxproj.filters` add a filter and entries:
```xml
    <Filter Include="System\Ps2\GS\ShaderChain">
      <UniqueIdentifier>{7c1f2a4e-3b9d-4e0a-9f6b-2d8c5a1e7b30}</UniqueIdentifier>
    </Filter>
```
(inside the `<ItemGroup>` of `<Filter>` elements near line 193), plus
```xml
    <ClCompile Include="GS\ShaderChain\ShaderPresets.cpp">
      <Filter>System\Ps2\GS\ShaderChain</Filter>
    </ClCompile>
```
in the `ClCompile` group and
```xml
    <ClInclude Include="GS\ShaderChain\ShaderPresets.h">
      <Filter>System\Ps2\GS\ShaderChain</Filter>
    </ClInclude>
```
in the `ClInclude` group.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd ~/work/pcsx2 && cmake --build build --target core_test 2>&1 | tail -1 && ./build/tests/ctest/core/core_test --gtest_filter='ShaderPresets.*'
```
Expected: `[  PASSED  ] 6 tests.` (If the binary is elsewhere, `find build -name core_test -type f`.)

- [ ] **Step 5: Commit**

```bash
git add pcsx2/GS/ShaderChain/ShaderPresets.h pcsx2/GS/ShaderChain/ShaderPresets.cpp tests/ctest/core/shader_presets_tests.cpp tests/ctest/core/CMakeLists.txt pcsx2/CMakeLists.txt pcsx2/pcsx2.vcxproj pcsx2/pcsx2.vcxproj.filters
git commit -m "GS/ShaderChain: Add preset enumeration, path resolution and parameter store

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: LibrashaderLoader (runtime load, ABI check, common function table)

**Files:**
- Create: `pcsx2/GS/ShaderChain/LibrashaderLoader.h`, `pcsx2/GS/ShaderChain/LibrashaderLoader.cpp`
- Create: `tests/ctest/core/librashader_loader_tests.cpp`
- Modify: `tests/ctest/core/CMakeLists.txt`, `pcsx2/CMakeLists.txt` (source lists next to `GS/ShaderChain/ShaderPresets.*`), `pcsx2/pcsx2.vcxproj`, `pcsx2/pcsx2.vcxproj.filters`

**Interfaces:**
- Consumes: `librashader.h` (Task 1), `DynamicLibrary`, `EmuFolders::AppRoot`, `CocoaTools::GetBundlePath()` (macOS).
- Produces:
```cpp
namespace ShaderChain {
	struct Availability { bool available = false; std::string reason; };
	const Availability& GetAvailability();        // loads once, thread-safe, cached
	void* GetSymbol(const char* name);            // nullptr when unavailable or symbol missing
	struct CommonFunctions {                       // always-present (runtime-independent) entry points
		PFN_libra_abi_version abi_version; PFN_libra_api_version api_version;
		PFN_libra_error_errno error_errno; PFN_libra_error_write error_write;
		PFN_libra_error_free_string error_free_string; PFN_libra_error_free error_free;
		PFN_libra_preset_ctx_create preset_ctx_create; PFN_libra_preset_ctx_free preset_ctx_free;
		PFN_libra_preset_ctx_set_core_name preset_ctx_set_core_name; PFN_libra_preset_ctx_set_runtime preset_ctx_set_runtime;
		PFN_libra_preset_create_with_options preset_create_with_options; PFN_libra_preset_free preset_free;
		PFN_libra_preset_get_runtime_params preset_get_runtime_params; PFN_libra_preset_free_runtime_params preset_free_runtime_params;
	};
	const CommonFunctions& Common();               // valid only when GetAvailability().available
	std::string DescribeAndFreeError(libra_error_t error); // "unknown librashader error" when null; frees error
	std::string GetDefaultLibraryPath();
	Availability LoadFromPath(const std::string& path);   // test hook; does not touch global state
}
```
Backend-specific entry points (`libra_vk_*`, `libra_mtl_*`, `libra_d3d11_*`, `libra_d3d12_*`) are resolved by each backend via `GetSymbol()` and cast to the `PFN_libra_*` typedefs after defining its own `LIBRA_RUNTIME_*` macro, because those typedefs only exist under the runtime guards (Metal's additionally needs an Objective-C++ TU).

- [ ] **Step 1: Write the failing tests**

Create `tests/ctest/core/librashader_loader_tests.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/LibrashaderLoader.h"
#include "common/Path.h"
#include <gtest/gtest.h>

TEST(LibrashaderLoader, MissingLibraryIsUnavailableWithReason)
{
	const std::string path = "/definitely/not/here/librashader.dylib";
	const ShaderChain::Availability avail = ShaderChain::LoadFromPath(path);
	EXPECT_FALSE(avail.available);
	EXPECT_NE(avail.reason.find("librashader"), std::string::npos);
	EXPECT_NE(avail.reason.find(path), std::string::npos);
}

TEST(LibrashaderLoader, WrongLibraryIsUnavailableBecauseSymbolsAreMissing)
{
#ifdef __APPLE__
	const std::string path = "/usr/lib/libSystem.B.dylib";
#elif defined(_WIN32)
	const std::string path = "C:\\Windows\\System32\\kernel32.dll";
#else
	const std::string path = "libc.so.6";
#endif
	const ShaderChain::Availability avail = ShaderChain::LoadFromPath(path);
	EXPECT_FALSE(avail.available);
	EXPECT_NE(avail.reason.find("libra_abi_version"), std::string::npos);
}

TEST(LibrashaderLoader, DefaultPathHasPlatformFileName)
{
	const std::string path = ShaderChain::GetDefaultLibraryPath();
#ifdef _WIN32
	EXPECT_TRUE(path.ends_with("librashader.dll")) << path;
#elif defined(__APPLE__)
	EXPECT_TRUE(path.ends_with("librashader.dylib")) << path;
#else
	EXPECT_TRUE(path.ends_with("librashader.so")) << path;
#endif
}

TEST(LibrashaderLoader, DescribeNullErrorDoesNotCrash)
{
	EXPECT_EQ(ShaderChain::DescribeAndFreeError(nullptr), "unknown librashader error");
}
```
Add `librashader_loader_tests.cpp` to the `add_pcsx2_test(core_test ...)` list in `tests/ctest/core/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd ~/work/pcsx2 && cmake --build build --target core_test 2>&1 | grep -m1 -i "error"
```
Expected: compile error, `GS/ShaderChain/LibrashaderLoader.h: No such file`.

- [ ] **Step 3: Implement the loader**

Create `pcsx2/GS/ShaderChain/LibrashaderLoader.h`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Runtime-independent part of the librashader C API. Backends define their own
// LIBRA_RUNTIME_* macro and include librashader.h themselves for the chain functions.
#include "librashader.h"

#include <string>

namespace ShaderChain
{
	struct Availability
	{
		bool available = false;
		std::string reason;
	};

	struct CommonFunctions
	{
		PFN_libra_abi_version abi_version = nullptr;
		PFN_libra_api_version api_version = nullptr;
		PFN_libra_error_errno error_errno = nullptr;
		PFN_libra_error_write error_write = nullptr;
		PFN_libra_error_free_string error_free_string = nullptr;
		PFN_libra_error_free error_free = nullptr;
		PFN_libra_preset_ctx_create preset_ctx_create = nullptr;
		PFN_libra_preset_ctx_free preset_ctx_free = nullptr;
		PFN_libra_preset_ctx_set_core_name preset_ctx_set_core_name = nullptr;
		PFN_libra_preset_ctx_set_runtime preset_ctx_set_runtime = nullptr;
		PFN_libra_preset_create_with_options preset_create_with_options = nullptr;
		PFN_libra_preset_free preset_free = nullptr;
		PFN_libra_preset_get_runtime_params preset_get_runtime_params = nullptr;
		PFN_libra_preset_free_runtime_params preset_free_runtime_params = nullptr;
	};

	/// Loads the library on first call (thread-safe) and caches the result.
	const Availability& GetAvailability();

	/// Raw symbol lookup for backend-specific entry points. Returns nullptr if unavailable.
	void* GetSymbol(const char* name);

	/// Runtime-independent entry points. Only valid when GetAvailability().available.
	const CommonFunctions& Common();

	/// Converts a libra_error_t into a human readable string and frees it. Safe on null.
	std::string DescribeAndFreeError(libra_error_t error);

	/// Platform default location: next to the executable (Windows) or Contents/Frameworks (macOS bundle).
	std::string GetDefaultLibraryPath();

	/// Attempts to load from an explicit path without affecting global state. Used by tests.
	Availability LoadFromPath(const std::string& path);
} // namespace ShaderChain
```

Create `pcsx2/GS/ShaderChain/LibrashaderLoader.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/LibrashaderLoader.h"
#include "Config.h"

#include "common/Console.h"
#include "common/DynamicLibrary.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#ifdef __APPLE__
#include "common/CocoaTools.h"
#endif

#include "fmt/format.h"

#include <mutex>

namespace
{
	struct LoadedLibrary
	{
		DynamicLibrary lib;
		ShaderChain::CommonFunctions fns;
		ShaderChain::Availability avail;
	};

	template <typename T>
	bool Resolve(DynamicLibrary& lib, const char* name, T* out, std::string* reason)
	{
		if (lib.GetSymbol(name, out))
			return true;
		*reason = fmt::format("librashader is missing symbol {}", name);
		return false;
	}

	bool LoadInto(LoadedLibrary& l, const std::string& path)
	{
		l.avail = {};
		if (path.empty())
		{
			l.avail.reason = "librashader path is empty";
			return false;
		}

		Error error;
		if (!l.lib.Open(path.c_str(), &error))
		{
			l.avail.reason = fmt::format("librashader could not be loaded from {}: {}", path, error.GetDescription());
			return false;
		}

		std::string& r = l.avail.reason;
		ShaderChain::CommonFunctions& f = l.fns;
		if (!Resolve(l.lib, "libra_abi_version", &f.abi_version, &r) ||
			!Resolve(l.lib, "libra_api_version", &f.api_version, &r))
		{
			l.lib.Close();
			return false;
		}

		const LIBRASHADER_ABI_VERSION abi = f.abi_version();
		if (abi != LIBRASHADER_CURRENT_ABI)
		{
			r = fmt::format("librashader ABI {} does not match the expected ABI {}", abi, LIBRASHADER_CURRENT_ABI);
			l.lib.Close();
			return false;
		}

		if (!Resolve(l.lib, "libra_error_errno", &f.error_errno, &r) ||
			!Resolve(l.lib, "libra_error_write", &f.error_write, &r) ||
			!Resolve(l.lib, "libra_error_free_string", &f.error_free_string, &r) ||
			!Resolve(l.lib, "libra_error_free", &f.error_free, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_create", &f.preset_ctx_create, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_free", &f.preset_ctx_free, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_set_core_name", &f.preset_ctx_set_core_name, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_set_runtime", &f.preset_ctx_set_runtime, &r) ||
			!Resolve(l.lib, "libra_preset_create_with_options", &f.preset_create_with_options, &r) ||
			!Resolve(l.lib, "libra_preset_free", &f.preset_free, &r) ||
			!Resolve(l.lib, "libra_preset_get_runtime_params", &f.preset_get_runtime_params, &r) ||
			!Resolve(l.lib, "libra_preset_free_runtime_params", &f.preset_free_runtime_params, &r))
		{
			l.lib.Close();
			return false;
		}

		l.avail.available = true;
		l.avail.reason.clear();
		return true;
	}

	LoadedLibrary& Global()
	{
		static LoadedLibrary s_lib;
		return s_lib;
	}

	std::once_flag s_load_once;
} // namespace

std::string ShaderChain::GetDefaultLibraryPath()
{
#ifdef _WIN32
	return Path::Combine(EmuFolders::AppRoot, "librashader.dll");
#elif defined(__APPLE__)
	if (const std::optional<std::string> bundle = CocoaTools::GetBundlePath(); bundle.has_value())
	{
		std::string in_bundle = Path::Combine(*bundle, "Contents/Frameworks/librashader.dylib");
		if (FileSystem::FileExists(in_bundle.c_str()))
			return in_bundle;
	}
	return Path::Combine(EmuFolders::AppRoot, "librashader.dylib");
#else
	return Path::Combine(EmuFolders::AppRoot, "librashader.so");
#endif
}

ShaderChain::Availability ShaderChain::LoadFromPath(const std::string& path)
{
	LoadedLibrary local;
	LoadInto(local, path);
	return local.avail; // local.lib closes on scope exit
}

const ShaderChain::Availability& ShaderChain::GetAvailability()
{
	std::call_once(s_load_once, []() {
		LoadedLibrary& g = Global();
		const std::string path = GetDefaultLibraryPath();
		if (LoadInto(g, path))
			INFO_LOG("librashader loaded from {} (ABI {}, API {})", path, g.fns.abi_version(), g.fns.api_version());
		else
			WARNING_LOG("Shader chain unavailable: {}", g.avail.reason);
	});
	return Global().avail;
}

void* ShaderChain::GetSymbol(const char* name)
{
	if (!GetAvailability().available)
		return nullptr;
	return Global().lib.GetSymbolAddress(name);
}

const ShaderChain::CommonFunctions& ShaderChain::Common()
{
	GetAvailability();
	return Global().fns;
}

std::string ShaderChain::DescribeAndFreeError(libra_error_t error)
{
	if (!error)
		return "unknown librashader error";

	const CommonFunctions& f = Common();
	if (!f.error_write)
		return "librashader error (library not loaded)";

	std::string result;
	char* text = nullptr;
	if (f.error_write(error, &text) == 0 && text)
	{
		result = text;
		f.error_free_string(&text);
	}
	else
	{
		result = fmt::format("librashader error code {}", static_cast<int>(f.error_errno(error)));
	}
	f.error_free(&error);
	return result;
}
```

Register `GS/ShaderChain/LibrashaderLoader.cpp` and `.h` in `pcsx2/CMakeLists.txt` (next to the `ShaderPresets` entries), `pcsx2/pcsx2.vcxproj` (`ClCompile` / `ClInclude`) and `pcsx2/pcsx2.vcxproj.filters` (filter `System\Ps2\GS\ShaderChain`), exactly as done for `ShaderPresets` in Task 3.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd ~/work/pcsx2 && cmake --build build --target core_test 2>&1 | tail -1 && ./build/tests/ctest/core/core_test --gtest_filter='LibrashaderLoader.*'
```
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 5: Commit**

```bash
git add pcsx2/GS/ShaderChain/LibrashaderLoader.h pcsx2/GS/ShaderChain/LibrashaderLoader.cpp tests/ctest/core/librashader_loader_tests.cpp tests/ctest/core/CMakeLists.txt pcsx2/CMakeLists.txt pcsx2/pcsx2.vcxproj pcsx2/pcsx2.vcxproj.filters
git commit -m "GS/ShaderChain: Add runtime loader for librashader with ABI check

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Build librashader in the macOS deps script and bundle it (CMake)

**Files:**
- Modify: `.github/workflows/scripts/macos/build-dependencies-universal.sh` (add version variables after its `SHADERC_SPIRVTOOLS=...` line; add the build step after the shaderc step, before `echo "Installing Qt Translations..."`)
- Modify: `.github/workflows/scripts/macos/build-dependencies.sh` (same insertion points; x86_64-only script, step is replaced by a skip message, see Step 2)
- Create: `cmake/FindLibrashader.cmake`
- Modify: `cmake/BuildParameters.cmake:22` (after `option(USE_VULKAN ...)`), `cmake/SearchForStuff.cmake:32` (after the Shaderc block), `pcsx2/CMakeLists.txt` (define near the include dir added in Task 1; bundle step after the MoltenVK block at lines 1408-1421)

**Interfaces:**
- Consumes: Rust `cargo`/`rustup` on the deps build host (present here: cargo 1.98).
- Produces: `$INSTALLDIR/lib/librashader.dylib` with id `@rpath/librashader.dylib`; CMake variable `LIBRASHADER_LIBRARY` and imported target `Librashader::librashader`; compile definition `PCSX2_HAS_LIBRASHADER=1`; `PCSX2.app/Contents/Frameworks/librashader.dylib`.

Prerequisite: `~/deps` is produced by `build-dependencies-universal.sh ~/deps` (over two hours; required for any PCSX2 build on Apple Silicon). The librashader step alone takes a few minutes and can be iterated on separately with the snippet in Step 3.

- [ ] **Step 1: Add the librashader step to `build-dependencies-universal.sh`**

This is the script CI uses for arm64 macOS builds and the one used on the development Mac (Apple Silicon); it produces fat x86_64+arm64 dependencies. librashader is built for the host architecture only (arm64), which is the phase 1 target. After its `SHADERC_SPIRVTOOLS=...` line add:
```bash
LIBRASHADER=0.12.0
LIBRASHADER_RUST=1.88
```
After the shaderc block's `cd ..` (the line before `echo "Installing Qt Translations..."`) add:
```bash
echo "Building librashader (host architecture only)..."
if ! command -v cargo >/dev/null 2>&1 || ! command -v rustup >/dev/null 2>&1; then
	echo "cargo/rustup not found on PATH; install Rust from https://rustup.rs and re-run." >&2
	exit 1
fi
rustup toolchain install "$LIBRASHADER_RUST" --profile minimal
rm -fr "librashader-$LIBRASHADER"
git clone --depth 1 --branch "librashader-v$LIBRASHADER" https://github.com/SnowflakePowered/librashader.git "librashader-$LIBRASHADER"
cd "librashader-$LIBRASHADER"
# Only the runtimes PCSX2 uses on macOS. Never enable runtime-d3d9.
rustup run "$LIBRASHADER_RUST" cargo build -p librashader-capi --profile optimized --no-default-features --features runtime-vulkan,runtime-metal
cp target/optimized/liblibrashader_capi.dylib "$INSTALLDIR/lib/librashader.dylib"
install_name_tool -id @rpath/librashader.dylib "$INSTALLDIR/lib/librashader.dylib"
codesign --force --sign - "$INSTALLDIR/lib/librashader.dylib"
mkdir -p "$INSTALLDIR/include"
cp include/librashader.h "$INSTALLDIR/include/librashader.h"
cd ..
```
Note: the header copy into deps is informational; the build uses the vendored copy from Task 1. The dylib is single-arch, so `merge_binaries` never touches it (it only merges Mach-O x86_64 files found in the x86 build dir).

- [ ] **Step 2: Document the gap in `build-dependencies.sh`**

`build-dependencies.sh` builds x86_64-only dependencies (used by CI's x86-64 job); macOS x86_64 is out of scope. Add the same two variables after its `SHADERC_SPIRVTOOLS` line and, at the same position as Step 1, add only:
```bash
echo "Skipping librashader in the x86_64-only build (macOS x86_64 is out of scope for the shader chain)."
```
This keeps the CI cache-key change deliberate and documents the gap.

- [ ] **Step 3: Run the librashader step standalone to validate it**

```bash
mkdir -p ~/deps/lib ~/deps/include && cd /tmp && rm -rf librashader-0.12.0 && \
rustup toolchain install 1.88 --profile minimal && \
git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git librashader-0.12.0 && cd librashader-0.12.0 && \
rustup run 1.88 cargo build -p librashader-capi --profile optimized --no-default-features --features runtime-vulkan,runtime-metal && \
cp target/optimized/liblibrashader_capi.dylib ~/deps/lib/librashader.dylib && install_name_tool -id @rpath/librashader.dylib ~/deps/lib/librashader.dylib && codesign --force --sign - ~/deps/lib/librashader.dylib && \
otool -D ~/deps/lib/librashader.dylib && nm -gU ~/deps/lib/librashader.dylib | grep -c " _libra_" && nm -gU ~/deps/lib/librashader.dylib | grep -c "_libra_d3d"
```
Expected: `@rpath/librashader.dylib`; a count of at least 40 `_libra_` exports; `0` d3d exports. If the `--profile optimized` name is rejected, the workspace profile was renamed upstream; use `--release` and `target/release/`. Then run the full deps script once: `.github/workflows/scripts/macos/build-dependencies-universal.sh ~/deps` (the x86_64-only `build-dependencies.sh` cannot produce arm64 libraries). Both scripts need Homebrew `nasm` for FFmpeg and full Xcode for MoltenVK.

- [ ] **Step 4: Add CMake discovery, option, define and bundling**

Create `cmake/FindLibrashader.cmake`:
```cmake
# - Try to find librashader (runtime-loaded shared library)
# Once done this will define
#  LIBRASHADER_FOUND - System has librashader
#  LIBRASHADER_LIBRARY - Path to librashader.dylib / librashader.dll

find_library(
    LIBRASHADER_LIBRARY
    NAMES librashader.dylib librashader.dll librashader
    PATHS ${ADDITIONAL_LIBRARY_PATHS}
    PATH_SUFFIXES lib bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Librashader DEFAULT_MSG LIBRASHADER_LIBRARY)

if(LIBRASHADER_FOUND)
    add_library(Librashader::librashader UNKNOWN IMPORTED)
    set_target_properties(Librashader::librashader PROPERTIES IMPORTED_LOCATION ${LIBRASHADER_LIBRARY})
endif()

mark_as_advanced(LIBRASHADER_LIBRARY)
```

In `cmake/BuildParameters.cmake` after line 22 (`option(USE_VULKAN ...)`):
```cmake
if(WIN32 OR APPLE)
	option(USE_LIBRASHADER "Enable librashader post-processing shader chain (library loaded at runtime)" ON)
endif()
```

In `cmake/SearchForStuff.cmake` after the `if(USE_VULKAN) find_package(Shaderc REQUIRED) endif()` block:
```cmake
if(USE_LIBRASHADER)
	find_package(Librashader)
	if(NOT LIBRASHADER_FOUND)
		message(WARNING "USE_LIBRASHADER is ON but librashader was not found in CMAKE_PREFIX_PATH; the shader chain will report unavailable at runtime.")
	endif()
endif()
```

In `pcsx2/CMakeLists.txt`, after the include-directory line added in Task 1:
```cmake
if(USE_LIBRASHADER)
	target_compile_definitions(PCSX2_FLAGS INTERFACE PCSX2_HAS_LIBRASHADER=1)
endif()
```
and inside `setup_main_executable()` after the MoltenVK block (after its `endif()` at line 1421):
```cmake
		# Copy librashader into the bundle (loaded at runtime by GS/ShaderChain/LibrashaderLoader.cpp)
		if(USE_LIBRASHADER AND LIBRASHADER_FOUND)
			target_sources(${target} PRIVATE "${LIBRASHADER_LIBRARY}")
			set_source_files_properties("${LIBRASHADER_LIBRARY}" PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
			message(STATUS "Using librashader from ${LIBRASHADER_LIBRARY}")
		endif()
```

- [ ] **Step 5: Configure, build, and verify the bundle and loader**

```bash
cd ~/work/pcsx2 && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Devel -DCMAKE_PREFIX_PATH="$HOME/deps" -DCMAKE_OSX_ARCHITECTURES=arm64 -DDISABLE_ADVANCE_SIMD=ON 2>&1 | grep -i "librashader" && cmake --build build --target pcsx2-qt 2>&1 | tail -1 && ls -l build/bin/PCSX2.app/Contents/Frameworks/librashader.dylib && open build/bin/PCSX2.app && sleep 8 && osascript -e 'quit app "PCSX2"'; grep -i "librashader" ~/Library/Application\ Support/PCSX2/logs/emulog.txt | tail -2
```
Expected: `Using librashader from .../deps/lib/librashader.dylib`; the dylib present in `Frameworks`; and, once Task 6 wires `GetAvailability()` into the GS thread, a log line `librashader loaded from ... (ABI 2, API 5)`. Until Task 6 the log line is absent; the bundle check is the gate for this task.

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/scripts/macos/build-dependencies.sh .github/workflows/scripts/macos/build-dependencies-universal.sh cmake/FindLibrashader.cmake cmake/BuildParameters.cmake cmake/SearchForStuff.cmake pcsx2/CMakeLists.txt
git commit -m "Build: Build librashader 0.12.0 in macOS deps and bundle it

Adds USE_LIBRASHADER (default ON on Windows/macOS), FindLibrashader.cmake,
and copies librashader.dylib into Contents/Frameworks.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: GSDevice core integration and VSync hook

**Files:**
- Modify: `pcsx2/GS/Renderers/Common/GSDevice.h:1494-1500` (members), `:1508-1516` (protected virtuals), `:1716-1724` (public post-processing API)
- Modify: `pcsx2/GS/Renderers/Common/GSDevice.cpp:388-392` (`Destroy`), `:925-942` (`ClearCurrent`), after `CAS` at `:1203`
- Modify: `pcsx2/GS/Renderers/Common/GSRenderer.cpp:640-646` (skip branch), `:670-688` (CAS block), `:698-699` (`PresentRect` call)

**Interfaces:**
- Consumes: `ShaderChain::GetAvailability()` (Task 4), `ShaderPresets::ResolvePresetPath` (Task 3), `GSConfig.ShaderChainEnabled/ShaderChainPreset` (Task 2), `FilteredDownsampleTexture`, `StretchRect`, `ResizeRenderTarget`.
- Produces (in `GSDevice`):
```cpp
protected:
	GSTexture* m_shader_chain_source = nullptr;
	GSTexture* m_shader_chain_target = nullptr;
	u64 m_shader_chain_frame_count = 0;
	/// Backend hook. Runs the chain from sTex (native res, shader-readable) into dTex (render target).
	/// Default returns false, meaning "no chain rendered this frame".
	virtual bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) { return false; }
	/// Backend hook. Frees any chain object; called on disable, preset change and Destroy().
	virtual void ReleaseShaderChain() {}
public:
	/// Returns true and rewrites current/src_rect/src_uv when the chain produced a frame.
	bool ApplyShaderChain(GSTexture*& current, GSVector4i& src_rect, GSVector4& src_uv, const GSVector4& draw_rect, const GSVector2i& native_size);
	void NoteShaderChainFrameSkipped() { m_shader_chain_frame_count++; }
	/// Path the backends should load; empty means disabled. Resolved once per frame in ApplyShaderChain.
	const std::string& GetShaderChainPresetPath() const { return m_shader_chain_preset_path; }
```
No unit test harness exists for `GSDevice`; the gate is a clean build and a no-regression run with the chain disabled, plus a debug log confirming the code path runs when enabled (backends still return false until Tasks 7-10).

- [ ] **Step 1: Declare members and hooks in `GSDevice.h`**

After line 1500 (`GSTexture* m_ds_as_rt = nullptr; ///< Depth as color`) add:
```cpp
	GSTexture* m_shader_chain_source = nullptr; ///< Native-resolution copy fed to librashader
	GSTexture* m_shader_chain_target = nullptr; ///< librashader output, draw-rect sized
	u64 m_shader_chain_frame_count = 0;
	std::string m_shader_chain_preset_path; ///< Absolute path of the preset the backend should have loaded
	bool m_shader_chain_failed_logged = false;
```
After line 1516 (`virtual bool DoCAS(...) = 0;`) add:
```cpp
	/// Runs the librashader chain from sTex (native resolution, shader readable) into dTex (render target).
	/// Backends that support it override this; the default means "no chain rendered".
	virtual bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) { return false; }

	/// Frees the backend chain object. Called on disable, preset change, and Destroy().
	virtual void ReleaseShaderChain() {}
```
After line 1724 (`void CAS(...)`) add:
```cpp
	/// Applies the librashader shader chain for presentation. On success, rewrites tex/src_rect/src_uv
	/// to the chain output and returns true; the caller must then skip CAS and the TV shader.
	bool ApplyShaderChain(GSTexture*& tex, GSVector4i& src_rect, GSVector4& src_uv, const GSVector4& draw_rect,
		const GSVector2i& native_size);

	/// Advances the chain frame counter for a frame that was not presented, so phase-based shaders keep cadence.
	void NoteShaderChainFrameSkipped() { m_shader_chain_frame_count++; }

	/// Absolute preset path the chain should be using, or empty when disabled.
	const std::string& GetShaderChainPresetPath() const { return m_shader_chain_preset_path; }
```

- [ ] **Step 2: Implement lifecycle and `ApplyShaderChain` in `GSDevice.cpp`**

Add includes after `#include "GS/GSUtil.h"`:
```cpp
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "GS/ShaderChain/ShaderPresets.h"
```
Change `Destroy()` (line 388) to:
```cpp
void GSDevice::Destroy()
{
	ReleaseShaderChain();
	ClearCurrent();
	PurgePool();
}
```
In `ClearCurrent()` add after `delete m_cas;`:
```cpp
	delete m_shader_chain_source;
	delete m_shader_chain_target;
```
and after `m_cas = nullptr;`:
```cpp
	m_shader_chain_source = nullptr;
	m_shader_chain_target = nullptr;
```
After the `CAS()` function (line 1203) add:
```cpp
bool GSDevice::ApplyShaderChain(GSTexture*& tex, GSVector4i& src_rect, GSVector4& src_uv, const GSVector4& draw_rect,
	const GSVector2i& native_size)
{
	// Resolve what the backend should be running this frame.
	std::string wanted;
	if (GSConfig.ShaderChainEnabled && !GSConfig.ShaderChainPreset.empty() && ShaderChain::GetAvailability().available)
		wanted = ShaderPresets::ResolvePresetPath(GSConfig.ShaderChainPreset);

	if (wanted != m_shader_chain_preset_path)
	{
		ReleaseShaderChain();
		m_shader_chain_preset_path = std::move(wanted);
		m_shader_chain_failed_logged = false;
	}

	if (m_shader_chain_preset_path.empty())
	{
		if (m_shader_chain_source || m_shader_chain_target)
		{
			delete m_shader_chain_source;
			delete m_shader_chain_target;
			m_shader_chain_source = nullptr;
			m_shader_chain_target = nullptr;
		}
		return false;
	}

	const int out_w = static_cast<int>(std::ceil(draw_rect.z - draw_rect.x));
	const int out_h = static_cast<int>(std::ceil(draw_rect.w - draw_rect.y));
	if (out_w <= 0 || out_h <= 0 || native_size.x <= 0 || native_size.y <= 0)
		return false;

	// 1. Downscale the (possibly upscaled) frame to native PCRTC resolution.
	if (!ResizeRenderTarget(&m_shader_chain_source, native_size.x, native_size.y, false, false))
		return false;

	const GSVector4 native_rect(0.0f, 0.0f, static_cast<float>(native_size.x), static_cast<float>(native_size.y));
	const int src_w = src_rect.width();
	const int src_h = src_rect.height();
	// Box-filter only for an uncropped integer upscale (the downsample shader samples from the origin);
	// anything else goes through a bilinear StretchRect using the cropped UVs.
	const bool integer_factor = (src_rect.x == 0) && (src_rect.y == 0) &&
	                            (src_w % native_size.x == 0) && (src_h % native_size.y == 0) &&
	                            (src_w / native_size.x == src_h / native_size.y) && (src_w / native_size.x) > 1;
	if (integer_factor)
	{
		const u32 factor = static_cast<u32>(src_w / native_size.x);
		FilteredDownsampleTexture(tex, m_shader_chain_source, factor, GSVector2i(0, 0), native_rect);
	}
	else
	{
		StretchRect(tex, src_uv, m_shader_chain_source, native_rect, ShaderConvert::COPY, Biln);
	}

	// 2. Run the chain into a draw-rect sized target.
	if (!ResizeRenderTarget(&m_shader_chain_target, out_w, out_h, false, false))
		return false;

	const u64 frame_count = m_shader_chain_frame_count++;
	if (!DoApplyShaderChain(m_shader_chain_source, m_shader_chain_target, frame_count))
	{
		if (!m_shader_chain_failed_logged)
		{
			WARNING_LOG("Shader chain did not render for preset {}; presenting unshaded frame.", m_shader_chain_preset_path);
			m_shader_chain_failed_logged = true;
		}
		return false;
	}

	tex = m_shader_chain_target;
	src_rect = GSVector4i(0, 0, out_w, out_h);
	src_uv = GSVector4(0.0f, 0.0f, 1.0f, 1.0f);
	return true;
}
```
`StretchRect(sTex, sRect, dTex, dRect, ShaderConvertSelector, Filter)` is declared at `GSDevice.h:1677`; `ShaderConvert::COPY` converts implicitly to `ShaderConvertSelector`, and `Biln` is the `Filter` enumerator (see the identical call at `GSDevice.cpp:1100`).

- [ ] **Step 3: Hook `GSRenderer::VSync`**

In the skip branch (line 640-646), add the counter bump:
```cpp
	if (skip_frame || g_gs_device->ShouldSkipPresentingFrame())
	{
		g_gs_device->NoteShaderChainFrameSkipped();
		if (BeginPresentFrame(true))
			EndPresentFrame();
```
Replace the CAS block (`if (GSConfig.CASMode != GSCASMode::Disabled) { ... }`, lines 670-688) with:
```cpp
			shader_chain_active = g_gs_device->ApplyShaderChain(current, src_rect, src_uv, draw_rect,
				PCRTCDisplays.GetResolution());

			if (!shader_chain_active && GSConfig.CASMode != GSCASMode::Disabled)
			{
				static bool cas_log_once = false;
				if (g_gs_device->Features().cas_sharpening)
				{
					// sharpen only if the IR is higher than the display resolution
					const bool sharpen_only = (GSConfig.CASMode == GSCASMode::SharpenOnly ||
					                           (current->GetWidth() > g_gs_device->GetWindowWidth() &&
					                            current->GetHeight() > g_gs_device->GetWindowHeight()));
					g_gs_device->CAS(current, src_rect, src_uv, draw_rect, sharpen_only);
				}
				else if (!cas_log_once)
				{
					Host::AddIconOSDMessage("CASUnsupported", ICON_FA_TRIANGLE_EXCLAMATION,
						TRANSLATE_SV("GS", "CAS is not available, your graphics driver does not support the required functionality."),
						10.0f);
					cas_log_once = true;
				}
			}
```
and declare `bool shader_chain_active = false;` next to `GSTexture* current = g_gs_device->GetCurrent();` (line 660). Change the `PresentRect` call (lines 698-699) to:
```cpp
				g_gs_device->PresentRect(current, src_uv, nullptr, draw_rect,
					shader_chain_active ? PresentShader::COPY : s_tv_shader_indices[GSConfig.TVShader],
					shader_chain_active ? 0.0f : shader_time,
					BilnIf(GSConfig.LinearPresent != GSPostBilinearMode::Off));
```

- [ ] **Step 4: Build and run a no-regression check**

```bash
cd ~/work/pcsx2 && cmake --build build --target pcsx2-qt 2>&1 | grep -E "error|warning: unused" | head; cmake --build build --target pcsx2-qt 2>&1 | tail -1
```
Expected: no errors. Launch a game with the chain disabled (default) and confirm rendering, CAS and TV shaders behave as before. Then set in the INI `ShaderChainEnabled = true` and `ShaderChainPreset = shaders_slang/nearest.slangp` (create an empty file at `~/Library/Application Support/PCSX2/shaders/shaders_slang/nearest.slangp` for now), launch, and confirm `emulog.txt` contains `librashader loaded from` and one `Shader chain did not render for preset` warning (backends return false until Tasks 7-10), with the game still visible.

- [ ] **Step 5: Commit**

```bash
git add pcsx2/GS/Renderers/Common/GSDevice.h pcsx2/GS/Renderers/Common/GSDevice.cpp pcsx2/GS/Renderers/Common/GSRenderer.cpp
git commit -m "GS: Add shader chain hooks to GSDevice and run it at present time

DoApplyShaderChain/ReleaseShaderChain virtuals with no-op defaults, native
resolution downscale, dedicated textures, frame counter, and a VSync hook that
bypasses CAS and the TV shader while a preset is active.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Vulkan backend override

**Files:**
- Modify: `pcsx2/GS/Renderers/Vulkan/GSDeviceVK.h` (declarations in the `private:` section that starts at line 697; overrides in the `public:` section near `void Destroy() override;` at line 562)
- Modify: `pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp` (includes at top; `Destroy()` body; new functions appended at end of file)

**Interfaces:**
- Consumes: `ShaderChain::GetSymbol`, `ShaderChain::Common()`, `ShaderChain::DescribeAndFreeError`, `GSDevice::GetShaderChainPresetPath()`, `ShaderPresets::Params()`, `GSTextureVK::{GetImage,GetVkFormat,TransitionToLayout,OverrideImageLayout}`, `InvalidateCachedState()`, `SetInitialState(VkCommandBuffer)`, `EndRenderPass()`, `ExecuteCommandBuffer(bool)`, `vkGetInstanceProcAddr` (global from `VKEntryPoints`).
- Produces: `bool GSDeviceVK::DoApplyShaderChain(GSTexture*, GSTexture*, u64) override;` `void GSDeviceVK::ReleaseShaderChain() override;`

No unit test; verified by running a preset on Vulkan (macOS via MoltenVK now, Windows in Task 11).

- [ ] **Step 1: Declare the override and state in `GSDeviceVK.h`**

In the `public:` section next to `void Destroy() override;` (line 562) add:
```cpp
	bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) override;
	void ReleaseShaderChain() override;
```
In the `private:` section (after line 697) add:
```cpp
	// librashader chain (opaque; librashader.h is only included in the .cpp)
	struct ShaderChainFunctions;
	void* m_shader_chain = nullptr;
	std::string m_shader_chain_loaded_path;
	bool m_shader_chain_failed = false;
	u64 m_shader_chain_params_generation = 0;
	bool EnsureShaderChain(const ShaderChainFunctions& fns);
	void ApplyShaderChainParams(const ShaderChainFunctions& fns);
```

- [ ] **Step 2: Implement in `GSDeviceVK.cpp`**

At the top, before `#include "GS/Renderers/Vulkan/GSDeviceVK.h"`, add:
```cpp
#define LIBRA_RUNTIME_VULKAN
#include "librashader.h"
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "GS/ShaderChain/ShaderPresets.h"
#include "IconsFontAwesome.h"
#include "fmt/format.h"
```
(`librashader.h` includes `<vulkan/vulkan.h>`; PCSX2 compiles with `VK_NO_PROTOTYPES` via `VKLoader.h`, which is fine because librashader only needs the types.)

In `Destroy()`, add `ReleaseShaderChain();` as the first statement after the `std::unique_lock lock(s_instance_mutex);` line and before `GSDevice::Destroy();` (the base also calls it, but the Vulkan version must wait for the GPU while the device is still alive).

Append at the end of the file:
```cpp
struct GSDeviceVK::ShaderChainFunctions
{
	PFN_libra_vk_filter_chain_create create = nullptr;
	PFN_libra_vk_filter_chain_frame frame = nullptr;
	PFN_libra_vk_filter_chain_set_param set_param = nullptr;
	PFN_libra_vk_filter_chain_free free = nullptr;

	bool Load()
	{
		create = reinterpret_cast<PFN_libra_vk_filter_chain_create>(ShaderChain::GetSymbol("libra_vk_filter_chain_create"));
		frame = reinterpret_cast<PFN_libra_vk_filter_chain_frame>(ShaderChain::GetSymbol("libra_vk_filter_chain_frame"));
		set_param = reinterpret_cast<PFN_libra_vk_filter_chain_set_param>(ShaderChain::GetSymbol("libra_vk_filter_chain_set_param"));
		free = reinterpret_cast<PFN_libra_vk_filter_chain_free>(ShaderChain::GetSymbol("libra_vk_filter_chain_free"));
		return create && frame && set_param && free;
	}
};

static const GSDeviceVK::ShaderChainFunctions& GetVKShaderChainFunctions()
{
	static GSDeviceVK::ShaderChainFunctions s_fns;
	static bool s_loaded = s_fns.Load();
	(void)s_loaded;
	return s_fns;
}

void GSDeviceVK::ReleaseShaderChain()
{
	if (!m_shader_chain)
	{
		m_shader_chain_loaded_path.clear();
		m_shader_chain_failed = false;
		return;
	}

	// The chain owns per-frame Vulkan objects; make sure nothing in flight references them.
	if (GetCurrentCommandBuffer() != VK_NULL_HANDLE)
		ExecuteCommandBuffer(true);

	auto chain = static_cast<libra_vk_filter_chain_t>(m_shader_chain);
	GetVKShaderChainFunctions().free(&chain);
	m_shader_chain = nullptr;
	m_shader_chain_loaded_path.clear();
	m_shader_chain_failed = false;
}

bool GSDeviceVK::EnsureShaderChain(const ShaderChainFunctions& fns)
{
	const std::string& wanted = GetShaderChainPresetPath();
	if (m_shader_chain && m_shader_chain_loaded_path == wanted)
		return true;
	if (m_shader_chain_failed && m_shader_chain_loaded_path == wanted)
		return false;

	ReleaseShaderChain();
	m_shader_chain_loaded_path = wanted;

	const ShaderChain::CommonFunctions& c = ShaderChain::Common();
	libra_preset_ctx_t ctx = nullptr;
	libra_shader_preset_t preset = nullptr;
	libra_error_t err = c.preset_ctx_create(&ctx);
	if (!err) err = c.preset_ctx_set_runtime(ctx, LIBRA_PRESET_CTX_RUNTIME_VULKAN);
	if (!err) err = c.preset_ctx_set_core_name(ctx, "PCSX2");
	if (!err) err = c.preset_create_with_options(wanted.c_str(), ctx, nullptr, &preset); // consumes ctx on success
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		if (ctx) c.preset_ctx_free(&ctx);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to load shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(VK): preset load failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	libra_device_vk_t vk = {};
	vk.physical_device = m_physical_device;
	vk.instance = m_instance;
	vk.device = m_device;
	vk.queue = m_graphics_queue;
	vk.entry = vkGetInstanceProcAddr;

	filter_chain_vk_opt_t opt = {};
	opt.version = LIBRASHADER_CURRENT_VERSION;
	opt.frames_in_flight = NUM_COMMAND_BUFFERS;
	opt.force_no_mipmaps = false;
	opt.use_dynamic_rendering = false;
	opt.disable_cache = false;

	// Chain creation uploads LUTs with its own submit; keep our recorded work ordered before it.
	EndRenderPass();

	libra_vk_filter_chain_t chain = nullptr;
	err = fns.create(&preset, vk, &opt, &chain); // consumes preset
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to compile shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(VK): chain create failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	m_shader_chain = chain;
	m_shader_chain_params_generation = 0; // force a param push on first frame
	INFO_LOG("ShaderChain(VK): loaded {}", wanted);
	return true;
}

void GSDeviceVK::ApplyShaderChainParams(const ShaderChainFunctions& fns)
{
	ShaderPresets::ParameterStore& store = ShaderPresets::Params();
	if (store.GetGeneration() == m_shader_chain_params_generation)
		return;

	std::string preset;
	ShaderPresets::ParameterStore::ParamList params;
	m_shader_chain_params_generation = store.Snapshot(&preset, &params);
	if (preset != GSConfig.ShaderChainPreset)
		return;

	auto chain = static_cast<libra_vk_filter_chain_t>(m_shader_chain);
	for (const auto& [name, value] : params)
	{
		if (libra_error_t err = fns.set_param(chain, name.c_str(), value))
			ShaderChain::DescribeAndFreeError(err); // unknown parameter names are ignored
	}
}

bool GSDeviceVK::DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count)
{
	const ShaderChainFunctions& fns = GetVKShaderChainFunctions();
	if (!fns.create || !EnsureShaderChain(fns))
		return false;

	ApplyShaderChainParams(fns);

	GSTextureVK* const src = static_cast<GSTextureVK*>(sTex);
	GSTextureVK* const dst = static_cast<GSTextureVK*>(dTex);

	// librashader records its own render passes into our command buffer.
	EndRenderPass();
	src->CommitClear();
	dst->CommitClear();
	src->TransitionToLayout(GSTextureVK::Layout::ShaderReadOnly);
	dst->TransitionToLayout(GSTextureVK::Layout::ColorAttachment);

	const libra_image_vk_t in = {src->GetImage(), src->GetVkFormat(), static_cast<u32>(src->GetWidth()), static_cast<u32>(src->GetHeight())};
	const libra_image_vk_t out = {dst->GetImage(), dst->GetVkFormat(), static_cast<u32>(dst->GetWidth()), static_cast<u32>(dst->GetHeight())};
	const libra_viewport_t vp = {0.0f, 0.0f, static_cast<u32>(dst->GetWidth()), static_cast<u32>(dst->GetHeight())};

	auto chain = static_cast<libra_vk_filter_chain_t>(m_shader_chain);
	libra_error_t err = fns.frame(chain, GetCurrentCommandBuffer(), static_cast<size_t>(frame_count), in, out, &vp, nullptr, nullptr);

	// librashader left the output in COLOR_ATTACHMENT_OPTIMAL and bound its own state.
	dst->OverrideImageLayout(GSTextureVK::Layout::ColorAttachment);
	dst->TransitionToLayout(GSTextureVK::Layout::ShaderReadOnly);
	dst->SetState(GSTexture::State::Dirty);
	InvalidateCachedState();
	SetInitialState(GetCurrentCommandBuffer());

	if (err)
	{
		ERROR_LOG("ShaderChain(VK): frame failed: {}", ShaderChain::DescribeAndFreeError(err));
		m_shader_chain_failed = true;
		return false;
	}
	return true;
}
```
`GSTextureVK::CommitClear()` (`GSTextureVK.h:63`) applies any deferred clear; it must run before handing the image to librashader so a pending clear is not lost.

- [ ] **Step 3: Build and validate on macOS Vulkan (MoltenVK)**

```bash
cd ~/work/pcsx2 && cmake --build build --target pcsx2-qt 2>&1 | grep -E " error" ; cmake --build build --target pcsx2-qt 2>&1 | tail -1
```
Then place a real preset: download `https://buildbot.libretro.com/assets/frontend/shaders_slang.zip` and extract it into `~/Library/Application Support/PCSX2/shaders/shaders_slang/` (the zip has no top-level folder). Set the INI to `ShaderChainEnabled = true`, `ShaderChainPreset = shaders_slang/crt/crt-geom.slangp`, `Renderer = 14` (Vulkan). Launch a game. Expected: CRT effect visible, OSD text crisp, `emulog.txt` shows `ShaderChain(VK): loaded`. Enable the Vulkan validation layer via the Advanced tab's "Use Debug Device" and confirm no `VUID-vkDestroyImageView-imageView-01026` or layout errors while playing and while switching presets. If that VUID appears, add `ExecuteCommandBuffer(false);` after `SetInitialState(...)` in `DoApplyShaderChain` and note it in the spec's section 6.1.

- [ ] **Step 4: Commit**

```bash
git add pcsx2/GS/Renderers/Vulkan/GSDeviceVK.h pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp
git commit -m "GS/VK: Implement librashader shader chain

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Metal backend override

**Files:**
- Modify: `pcsx2/GS/Renderers/Metal/GSDeviceMTL.h` (public override near `void Destroy() override;` line 402; private members near line 219-232)
- Modify: `pcsx2/GS/Renderers/Metal/GSDeviceMTL.mm` (includes; `Destroy()` at the top of the function; new functions appended)

**Interfaces:**
- Consumes: same `ShaderChain::*` API as Task 7; `GetRenderCmdBuf()`, `EndRenderPass()`, `FlushEncoders()`, `GSTextureMTL::GetTexture()`, `m_queue` (`MRCOwned<id<MTLCommandQueue>>`, converts implicitly to `id<MTLCommandQueue>`).
- Produces: `bool GSDeviceMTL::DoApplyShaderChain(GSTexture*, GSTexture*, u64) override;` `void GSDeviceMTL::ReleaseShaderChain() override;`

- [ ] **Step 1: Declare in `GSDeviceMTL.h`**

Next to `void Destroy() override;` add:
```cpp
	bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) override;
	void ReleaseShaderChain() override;
```
In the permanent-resources member block (after `MRCOwned<id<MTLCommandQueue>> m_queue;`) add:
```cpp
	// librashader chain (opaque; the Metal declarations need an Objective-C++ TU)
	struct ShaderChainFunctions;
	void* m_shader_chain = nullptr;
	std::string m_shader_chain_loaded_path;
	bool m_shader_chain_failed = false;
	u64 m_shader_chain_params_generation = 0;
	bool EnsureShaderChain(const ShaderChainFunctions& fns);
	void ApplyShaderChainParams(const ShaderChainFunctions& fns);
```

- [ ] **Step 2: Implement in `GSDeviceMTL.mm`**

Before `#include "GS/Renderers/Metal/GSDeviceMTL.h"` add:
```objc
#define LIBRA_RUNTIME_METAL
#include "librashader.h"
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "GS/ShaderChain/ShaderPresets.h"
#include "IconsFontAwesome.h"
#include "fmt/format.h"
```
In `Destroy()`, add `ReleaseShaderChain();` right after `FlushEncoders();` (first statement inside the autoreleasepool).

Append at the end of the file (inside no namespace):
```objc
struct GSDeviceMTL::ShaderChainFunctions
{
	PFN_libra_mtl_filter_chain_create create = nullptr;
	PFN_libra_mtl_filter_chain_frame frame = nullptr;
	PFN_libra_mtl_filter_chain_set_param set_param = nullptr;
	PFN_libra_mtl_filter_chain_free free = nullptr;

	bool Load()
	{
		create = reinterpret_cast<PFN_libra_mtl_filter_chain_create>(ShaderChain::GetSymbol("libra_mtl_filter_chain_create"));
		frame = reinterpret_cast<PFN_libra_mtl_filter_chain_frame>(ShaderChain::GetSymbol("libra_mtl_filter_chain_frame"));
		set_param = reinterpret_cast<PFN_libra_mtl_filter_chain_set_param>(ShaderChain::GetSymbol("libra_mtl_filter_chain_set_param"));
		free = reinterpret_cast<PFN_libra_mtl_filter_chain_free>(ShaderChain::GetSymbol("libra_mtl_filter_chain_free"));
		return create && frame && set_param && free;
	}
};

static const GSDeviceMTL::ShaderChainFunctions& GetMTLShaderChainFunctions()
{
	static GSDeviceMTL::ShaderChainFunctions s_fns;
	static bool s_loaded = s_fns.Load();
	(void)s_loaded;
	return s_fns;
}

void GSDeviceMTL::ReleaseShaderChain()
{ @autoreleasepool {
	if (!m_shader_chain)
	{
		m_shader_chain_loaded_path.clear();
		m_shader_chain_failed = false;
		return;
	}
	FlushEncoders(); // any encoded chain passes must be committed before the chain goes away
	auto chain = static_cast<libra_mtl_filter_chain_t>(m_shader_chain);
	GetMTLShaderChainFunctions().free(&chain);
	m_shader_chain = nullptr;
	m_shader_chain_loaded_path.clear();
	m_shader_chain_failed = false;
}}

bool GSDeviceMTL::EnsureShaderChain(const ShaderChainFunctions& fns)
{
	const std::string& wanted = GetShaderChainPresetPath();
	if (m_shader_chain && m_shader_chain_loaded_path == wanted)
		return true;
	if (m_shader_chain_failed && m_shader_chain_loaded_path == wanted)
		return false;

	ReleaseShaderChain();
	m_shader_chain_loaded_path = wanted;

	const ShaderChain::CommonFunctions& c = ShaderChain::Common();
	libra_preset_ctx_t ctx = nullptr;
	libra_shader_preset_t preset = nullptr;
	libra_error_t err = c.preset_ctx_create(&ctx);
	if (!err) err = c.preset_ctx_set_runtime(ctx, LIBRA_PRESET_CTX_RUNTIME_METAL);
	if (!err) err = c.preset_ctx_set_core_name(ctx, "PCSX2");
	if (!err) err = c.preset_create_with_options(wanted.c_str(), ctx, nullptr, &preset);
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		if (ctx) c.preset_ctx_free(&ctx);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to load shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(MTL): preset load failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	filter_chain_mtl_opt_t opt = {};
	opt.version = LIBRASHADER_CURRENT_VERSION;
	opt.force_no_mipmaps = false;

	libra_mtl_filter_chain_t chain = nullptr;
	err = fns.create(&preset, m_queue, &opt, &chain);
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to compile shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(MTL): chain create failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	m_shader_chain = chain;
	m_shader_chain_params_generation = 0;
	INFO_LOG("ShaderChain(MTL): loaded {}", wanted);
	return true;
}

void GSDeviceMTL::ApplyShaderChainParams(const ShaderChainFunctions& fns)
{
	ShaderPresets::ParameterStore& store = ShaderPresets::Params();
	if (store.GetGeneration() == m_shader_chain_params_generation)
		return;

	std::string preset;
	ShaderPresets::ParameterStore::ParamList params;
	m_shader_chain_params_generation = store.Snapshot(&preset, &params);
	if (preset != GSConfig.ShaderChainPreset)
		return;

	auto chain = static_cast<libra_mtl_filter_chain_t>(m_shader_chain);
	for (const auto& [name, value] : params)
	{
		if (libra_error_t err = fns.set_param(chain, name.c_str(), value))
			ShaderChain::DescribeAndFreeError(err);
	}
}

bool GSDeviceMTL::DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count)
{ @autoreleasepool {
	const ShaderChainFunctions& fns = GetMTLShaderChainFunctions();
	if (!fns.create || !EnsureShaderChain(fns))
		return false;

	ApplyShaderChainParams(fns);

	GSTextureMTL* const src = static_cast<GSTextureMTL*>(sTex);
	GSTextureMTL* const dst = static_cast<GSTextureMTL*>(dTex);
	src->FlushClears();
	dst->FlushClears();

	// Metal aborts if we hand over a command buffer with an open encoder.
	EndRenderPass();

	const libra_viewport_t vp = {0.0f, 0.0f, static_cast<u32>(dst->GetWidth()), static_cast<u32>(dst->GetHeight())};
	auto chain = static_cast<libra_mtl_filter_chain_t>(m_shader_chain);
	libra_error_t err = fns.frame(chain, GetRenderCmdBuf(), static_cast<size_t>(frame_count),
		src->GetTexture(), dst->GetTexture(), &vp, nullptr, nullptr);

	dst->SetState(GSTexture::State::Dirty);
	// The chain recycles per-frame resources on its own ring; commit now so a chain frame always ends a batch.
	FlushEncoders();

	if (err)
	{
		ERROR_LOG("ShaderChain(MTL): frame failed: {}", ShaderChain::DescribeAndFreeError(err));
		m_shader_chain_failed = true;
		return false;
	}
	return true;
}}
```
`GSTextureMTL::FlushClears()` (`GSTextureMTL.h:37`) applies deferred clears before librashader reads or writes the textures.

- [ ] **Step 3: Build and validate on Metal**

```bash
cd ~/work/pcsx2 && cmake --build build --target pcsx2-qt 2>&1 | grep -E " error" ; cmake --build build --target pcsx2-qt 2>&1 | tail -1
```
Set `Renderer = 17` (Metal) in the INI with the same preset as Task 7 and launch. Expected: CRT effect visible, OSD crisp, `ShaderChain(MTL): loaded` in the log, no Metal API validation abort (run once with `METAL_DEVICE_WRAPPER_TYPE=1` exported before `open`). Switch presets from the INI while paused/resumed and confirm the chain reloads.

- [ ] **Step 4: Commit**

```bash
git add pcsx2/GS/Renderers/Metal/GSDeviceMTL.h pcsx2/GS/Renderers/Metal/GSDeviceMTL.mm
git commit -m "GS/Metal: Implement librashader shader chain

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: D3D11 backend override (Windows)

**Files:**
- Modify: `pcsx2/GS/Renderers/DX11/GSDevice11.h` (override near `void Destroy() override;` line 323; members after `m_swap_chain_rtv` line 133)
- Modify: `pcsx2/GS/Renderers/DX11/GSDevice11.cpp` (includes; `Destroy()`; new functions appended)

**Interfaces:**
- Consumes: `ShaderChain::*` API (Task 4), `GSDevice::GetShaderChainPresetPath()`, `GSTexture11` conversion operators `operator ID3D11ShaderResourceView*()` / `operator ID3D11RenderTargetView*()` (`GSTexture11.h:40-41`), `GSDevice11::CommitClear(GSTexture*)` (`GSDevice11.h:362`), `m_state` cache (`GSDevice11.h:159-191`), `m_dev`, `m_ctx`.
- Produces: `bool GSDevice11::DoApplyShaderChain(GSTexture*, GSTexture*, u64) override;` `void GSDevice11::ReleaseShaderChain() override;`

Windows-only: edit on any machine, build and verify on Windows x64 after Task 11.

- [ ] **Step 1: Declare in `GSDevice11.h`**

Next to `void Destroy() override;`:
```cpp
	bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) override;
	void ReleaseShaderChain() override;
```
After `wil::com_ptr_nothrow<ID3D11RenderTargetView> m_swap_chain_rtv;`:
```cpp
	// librashader chain (opaque; librashader.h is included in the .cpp only)
	struct ShaderChainFunctions;
	void* m_shader_chain = nullptr;
	std::string m_shader_chain_loaded_path;
	bool m_shader_chain_failed = false;
	u64 m_shader_chain_params_generation = 0;
	bool EnsureShaderChain(const ShaderChainFunctions& fns);
	void ApplyShaderChainParams(const ShaderChainFunctions& fns);
	void ResyncStateAfterShaderChain();
```

- [ ] **Step 2: Implement in `GSDevice11.cpp`**

Before `#include "GSDevice11.h"` add:
```cpp
#define LIBRA_RUNTIME_D3D11
#include "librashader.h"
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "GS/ShaderChain/ShaderPresets.h"
#include "fmt/format.h"
```
In `Destroy()`, add `ReleaseShaderChain();` as the first statement (before `delete m_null_texture;`).

Append at the end of the file:
```cpp
struct GSDevice11::ShaderChainFunctions
{
	PFN_libra_d3d11_filter_chain_create create = nullptr;
	PFN_libra_d3d11_filter_chain_frame frame = nullptr;
	PFN_libra_d3d11_filter_chain_set_param set_param = nullptr;
	PFN_libra_d3d11_filter_chain_free free = nullptr;

	bool Load()
	{
		create = reinterpret_cast<PFN_libra_d3d11_filter_chain_create>(ShaderChain::GetSymbol("libra_d3d11_filter_chain_create"));
		frame = reinterpret_cast<PFN_libra_d3d11_filter_chain_frame>(ShaderChain::GetSymbol("libra_d3d11_filter_chain_frame"));
		set_param = reinterpret_cast<PFN_libra_d3d11_filter_chain_set_param>(ShaderChain::GetSymbol("libra_d3d11_filter_chain_set_param"));
		free = reinterpret_cast<PFN_libra_d3d11_filter_chain_free>(ShaderChain::GetSymbol("libra_d3d11_filter_chain_free"));
		return create && frame && set_param && free;
	}
};

static const GSDevice11::ShaderChainFunctions& GetD3D11ShaderChainFunctions()
{
	static GSDevice11::ShaderChainFunctions s_fns;
	static bool s_loaded = s_fns.Load();
	(void)s_loaded;
	return s_fns;
}

void GSDevice11::ReleaseShaderChain()
{
	if (!m_shader_chain)
	{
		m_shader_chain_loaded_path.clear();
		m_shader_chain_failed = false;
		return;
	}
	auto chain = static_cast<libra_d3d11_filter_chain_t>(m_shader_chain);
	GetD3D11ShaderChainFunctions().free(&chain);
	m_shader_chain = nullptr;
	m_shader_chain_loaded_path.clear();
	m_shader_chain_failed = false;
}

bool GSDevice11::EnsureShaderChain(const ShaderChainFunctions& fns)
{
	const std::string& wanted = GetShaderChainPresetPath();
	if (m_shader_chain && m_shader_chain_loaded_path == wanted)
		return true;
	if (m_shader_chain_failed && m_shader_chain_loaded_path == wanted)
		return false;

	ReleaseShaderChain();
	m_shader_chain_loaded_path = wanted;

	const ShaderChain::CommonFunctions& c = ShaderChain::Common();
	libra_preset_ctx_t ctx = nullptr;
	libra_shader_preset_t preset = nullptr;
	libra_error_t err = c.preset_ctx_create(&ctx);
	if (!err) err = c.preset_ctx_set_runtime(ctx, LIBRA_PRESET_CTX_RUNTIME_D3D11);
	if (!err) err = c.preset_ctx_set_core_name(ctx, "PCSX2");
	if (!err) err = c.preset_create_with_options(wanted.c_str(), ctx, nullptr, &preset);
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		if (ctx) c.preset_ctx_free(&ctx);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to load shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(D3D11): preset load failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	filter_chain_d3d11_opt_t opt = {};
	opt.version = LIBRASHADER_CURRENT_VERSION;
	opt.force_no_mipmaps = false;
	opt.disable_cache = false;

	libra_d3d11_filter_chain_t chain = nullptr;
	err = fns.create(&preset, m_dev.get(), &opt, &chain);
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to compile shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(D3D11): chain create failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	m_shader_chain = chain;
	m_shader_chain_params_generation = 0;
	INFO_LOG("ShaderChain(D3D11): loaded {}", wanted);
	return true;
}

void GSDevice11::ApplyShaderChainParams(const ShaderChainFunctions& fns)
{
	ShaderPresets::ParameterStore& store = ShaderPresets::Params();
	if (store.GetGeneration() == m_shader_chain_params_generation)
		return;

	std::string preset;
	ShaderPresets::ParameterStore::ParamList params;
	m_shader_chain_params_generation = store.Snapshot(&preset, &params);
	if (preset != GSConfig.ShaderChainPreset)
		return;

	auto chain = static_cast<libra_d3d11_filter_chain_t>(m_shader_chain);
	for (const auto& [name, value] : params)
	{
		if (libra_error_t err = fns.set_param(chain, name.c_str(), value))
			ShaderChain::DescribeAndFreeError(err);
	}
}

void GSDevice11::ResyncStateAfterShaderChain()
{
	// librashader restores the D3D11 pipeline state it touched, but our m_state cache must not
	// believe anything about render targets, shader resources or the viewport, otherwise the
	// next binding is skipped as "unchanged". Mirror what BeginPresent() does for the RTV.
	m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	if (m_state.rtv)
	{
		m_state.rtv->Release();
		m_state.rtv = nullptr;
	}
	m_state.current_rt = nullptr;
	if (m_state.dsv)
	{
		m_state.dsv->Release();
		m_state.dsv = nullptr;
	}
	m_state.current_ds = nullptr;
	if (m_state.dsv_as_rtv)
	{
		m_state.dsv_as_rtv->Release();
		m_state.dsv_as_rtv = nullptr;
	}
	m_state.current_ds_as_rt = nullptr;

	ID3D11ShaderResourceView* null_srvs[MAX_TEXTURES] = {};
	m_ctx->PSSetShaderResources(0, MAX_TEXTURES, null_srvs);
	m_state.ps_current_srv.fill(nullptr);
	m_state.ps_pending_srv.fill(nullptr);

	m_state.viewport = GSVector2i(0, 0);
	m_state.scissor = GSVector4i::zero();
}

bool GSDevice11::DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count)
{
	const ShaderChainFunctions& fns = GetD3D11ShaderChainFunctions();
	if (!fns.create || !EnsureShaderChain(fns))
		return false;

	ApplyShaderChainParams(fns);

	CommitClear(sTex);
	CommitClear(dTex);

	// The source must not be bound as a render target and the target must not be bound as an SRV.
	m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	ID3D11ShaderResourceView* null_srvs[MAX_TEXTURES] = {};
	m_ctx->PSSetShaderResources(0, MAX_TEXTURES, null_srvs);

	GSTexture11* const src = static_cast<GSTexture11*>(sTex);
	GSTexture11* const dst = static_cast<GSTexture11*>(dTex);
	ID3D11ShaderResourceView* const srv = *src;
	ID3D11RenderTargetView* const rtv = *dst;
	const libra_viewport_t vp = {0.0f, 0.0f, static_cast<u32>(dst->GetWidth()), static_cast<u32>(dst->GetHeight())};

	auto chain = static_cast<libra_d3d11_filter_chain_t>(m_shader_chain);
	libra_error_t err = fns.frame(chain, nullptr /* immediate context */, static_cast<size_t>(frame_count), srv, rtv, &vp, nullptr, nullptr);

	dst->SetState(GSTexture::State::Dirty);
	ResyncStateAfterShaderChain();

	if (err)
	{
		ERROR_LOG("ShaderChain(D3D11): frame failed: {}", ShaderChain::DescribeAndFreeError(err));
		m_shader_chain_failed = true;
		return false;
	}
	return true;
}
```
`MAX_TEXTURES` is the existing constant used by `m_state.ps_current_srv` (`GSDevice11.h:166`).

- [ ] **Step 3: Build and validate (Windows x64, after Task 11)**

Build `pcsx2-qt` in Visual Studio (`Release Clang|x64` or via `cmake --build build --config Release`). Set `Renderer = 3` (D3D11) with the Task 7 preset. Expected: CRT effect, crisp OSD, `ShaderChain(D3D11): loaded` in the log, no D3D11 debug-layer errors with "Use Debug Device" enabled. Verify the HW renderer still draws correctly after the first shaded frame (a stale `m_state` would show as missing textures or black RTs).

- [ ] **Step 4: Commit**

```bash
git add pcsx2/GS/Renderers/DX11/GSDevice11.h pcsx2/GS/Renderers/DX11/GSDevice11.cpp
git commit -m "GS/DX11: Implement librashader shader chain

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 10: D3D12 backend override (Windows)

**Files:**
- Modify: `pcsx2/GS/Renderers/DX12/GSDevice12.h` (override near `void Destroy() override;` line 512; members in the `private:` block at line 374)
- Modify: `pcsx2/GS/Renderers/DX12/GSDevice12.cpp` (includes; `Destroy()`; new functions appended)

**Interfaces:**
- Consumes: `ShaderChain::*` API, `GetCommandList().list4` (`ID3D12GraphicsCommandList4*`), `GetDescriptorAllocator().GetDescriptorHeap()`, `GetSamplerAllocator().GetDescriptorHeap()`, `GSTexture12::{GetResource, TransitionToState, CommitClear}`, `GSTexture12::ResourceState::{PixelShaderResource, RenderTarget}`, `EndRenderPass()`, `InvalidateCachedState()`, `ExecuteCommandList(bool)`, `m_device`.
- Produces: `bool GSDevice12::DoApplyShaderChain(GSTexture*, GSTexture*, u64) override;` `void GSDevice12::ReleaseShaderChain() override;`

Windows-only: build and verify on Windows x64 after Task 11. Requires `dxcompiler.dll` next to the executable (Task 11 copies it).

- [ ] **Step 1: Declare in `GSDevice12.h`**

Next to `void Destroy() override;`:
```cpp
	bool DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count) override;
	void ReleaseShaderChain() override;
```
In the `private:` block after `u32 m_current_swap_chain_buffer = 0;`:
```cpp
	// librashader chain (opaque; librashader.h is included in the .cpp only)
	struct ShaderChainFunctions;
	void* m_shader_chain = nullptr;
	std::string m_shader_chain_loaded_path;
	bool m_shader_chain_failed = false;
	u64 m_shader_chain_params_generation = 0;
	bool EnsureShaderChain(const ShaderChainFunctions& fns);
	void ApplyShaderChainParams(const ShaderChainFunctions& fns);
```

- [ ] **Step 2: Implement in `GSDevice12.cpp`**

Before `#include "GS/Renderers/DX12/GSDevice12.h"` add:
```cpp
#define LIBRA_RUNTIME_D3D12
#include "librashader.h"
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "GS/ShaderChain/ShaderPresets.h"
#include "IconsFontAwesome.h"
#include "fmt/format.h"
```
In `Destroy()`, put `ReleaseShaderChain();` as the first statement of `GSDevice12::Destroy()`, before `GSDevice::Destroy();`, so the wait-for-GPU happens while the command list still exists (the base class also calls the virtual, which is then a no-op).

Append at the end of the file:
```cpp
struct GSDevice12::ShaderChainFunctions
{
	PFN_libra_d3d12_filter_chain_create create = nullptr;
	PFN_libra_d3d12_filter_chain_frame frame = nullptr;
	PFN_libra_d3d12_filter_chain_set_param set_param = nullptr;
	PFN_libra_d3d12_filter_chain_free free = nullptr;

	bool Load()
	{
		create = reinterpret_cast<PFN_libra_d3d12_filter_chain_create>(ShaderChain::GetSymbol("libra_d3d12_filter_chain_create"));
		frame = reinterpret_cast<PFN_libra_d3d12_filter_chain_frame>(ShaderChain::GetSymbol("libra_d3d12_filter_chain_frame"));
		set_param = reinterpret_cast<PFN_libra_d3d12_filter_chain_set_param>(ShaderChain::GetSymbol("libra_d3d12_filter_chain_set_param"));
		free = reinterpret_cast<PFN_libra_d3d12_filter_chain_free>(ShaderChain::GetSymbol("libra_d3d12_filter_chain_free"));
		return create && frame && set_param && free;
	}
};

static const GSDevice12::ShaderChainFunctions& GetD3D12ShaderChainFunctions()
{
	static GSDevice12::ShaderChainFunctions s_fns;
	static bool s_loaded = s_fns.Load();
	(void)s_loaded;
	return s_fns;
}

void GSDevice12::ReleaseShaderChain()
{
	if (!m_shader_chain)
	{
		m_shader_chain_loaded_path.clear();
		m_shader_chain_failed = false;
		return;
	}

	// The chain owns per-frame descriptors and resources; drain the GPU before freeing them.
	if (GetCommandList().list4)
	{
		EndRenderPass();
		ExecuteCommandList(true);
	}

	auto chain = static_cast<libra_d3d12_filter_chain_t>(m_shader_chain);
	GetD3D12ShaderChainFunctions().free(&chain);
	m_shader_chain = nullptr;
	m_shader_chain_loaded_path.clear();
	m_shader_chain_failed = false;
}

bool GSDevice12::EnsureShaderChain(const ShaderChainFunctions& fns)
{
	const std::string& wanted = GetShaderChainPresetPath();
	if (m_shader_chain && m_shader_chain_loaded_path == wanted)
		return true;
	if (m_shader_chain_failed && m_shader_chain_loaded_path == wanted)
		return false;

	ReleaseShaderChain();
	m_shader_chain_loaded_path = wanted;

	const ShaderChain::CommonFunctions& c = ShaderChain::Common();
	libra_preset_ctx_t ctx = nullptr;
	libra_shader_preset_t preset = nullptr;
	libra_error_t err = c.preset_ctx_create(&ctx);
	if (!err) err = c.preset_ctx_set_runtime(ctx, LIBRA_PRESET_CTX_RUNTIME_D3D12);
	if (!err) err = c.preset_ctx_set_core_name(ctx, "PCSX2");
	if (!err) err = c.preset_create_with_options(wanted.c_str(), ctx, nullptr, &preset);
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		if (ctx) c.preset_ctx_free(&ctx);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to load shader preset: {}"), msg), Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(D3D12): preset load failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	filter_chain_d3d12_opt_t opt = {};
	opt.version = LIBRASHADER_CURRENT_VERSION;
	opt.force_hlsl_pipeline = false;
	opt.force_no_mipmaps = false;
	opt.disable_cache = false;
	opt.frames_in_flight = 3;

	// Creation submits LUT uploads on its own; keep our pending work ordered before it.
	EndRenderPass();
	ExecuteCommandList(false);

	libra_d3d12_filter_chain_t chain = nullptr;
	err = fns.create(&preset, m_device.get(), &opt, &chain);
	if (err)
	{
		const std::string msg = ShaderChain::DescribeAndFreeError(err);
		Host::AddIconOSDMessage("ShaderChain", ICON_FA_TRIANGLE_EXCLAMATION,
			fmt::format(TRANSLATE_FS("GS", "Failed to compile shader preset: {} (D3D12 requires dxcompiler.dll next to PCSX2)"), msg),
			Host::OSD_ERROR_DURATION);
		ERROR_LOG("ShaderChain(D3D12): chain create failed for {}: {}", wanted, msg);
		m_shader_chain_failed = true;
		return false;
	}

	m_shader_chain = chain;
	m_shader_chain_params_generation = 0;
	INFO_LOG("ShaderChain(D3D12): loaded {}", wanted);
	return true;
}

void GSDevice12::ApplyShaderChainParams(const ShaderChainFunctions& fns)
{
	ShaderPresets::ParameterStore& store = ShaderPresets::Params();
	if (store.GetGeneration() == m_shader_chain_params_generation)
		return;

	std::string preset;
	ShaderPresets::ParameterStore::ParamList params;
	m_shader_chain_params_generation = store.Snapshot(&preset, &params);
	if (preset != GSConfig.ShaderChainPreset)
		return;

	auto chain = static_cast<libra_d3d12_filter_chain_t>(m_shader_chain);
	for (const auto& [name, value] : params)
	{
		if (libra_error_t err = fns.set_param(chain, name.c_str(), value))
			ShaderChain::DescribeAndFreeError(err);
	}
}

bool GSDevice12::DoApplyShaderChain(GSTexture* sTex, GSTexture* dTex, u64 frame_count)
{
	const ShaderChainFunctions& fns = GetD3D12ShaderChainFunctions();
	if (!fns.create || !EnsureShaderChain(fns))
		return false;

	ApplyShaderChainParams(fns);

	GSTexture12* const src = static_cast<GSTexture12*>(sTex);
	GSTexture12* const dst = static_cast<GSTexture12*>(dTex);

	// librashader uses D3D12 render passes of its own; ours must be closed first.
	EndRenderPass();
	src->CommitClear();
	dst->CommitClear();
	src->TransitionToState(GSTexture12::ResourceState::PixelShaderResource);
	dst->TransitionToState(GSTexture12::ResourceState::RenderTarget);

	libra_image_d3d12_t in = {};
	in.image_type = LIBRA_D3D12_IMAGE_TYPE_RESOURCE;
	in.handle.resource = src->GetResource();
	libra_image_d3d12_t out = {};
	out.image_type = LIBRA_D3D12_IMAGE_TYPE_RESOURCE;
	out.handle.resource = dst->GetResource();
	const libra_viewport_t vp = {0.0f, 0.0f, static_cast<u32>(dst->GetWidth()), static_cast<u32>(dst->GetHeight())};

	auto chain = static_cast<libra_d3d12_filter_chain_t>(m_shader_chain);
	libra_error_t err = fns.frame(chain, GetCommandList().list4.get(), static_cast<size_t>(frame_count), in, out, &vp, nullptr, nullptr);

	// librashader bound its own descriptor heaps, root signature and pipeline. PCSX2 only sets heaps
	// at command list reset (see the SetDescriptorHeaps call in MoveToNextCommandList), so rebind them.
	dst->SetState(GSTexture::State::Dirty);
	InvalidateCachedState();
	ID3D12DescriptorHeap* heaps[2] = {GetDescriptorAllocator().GetDescriptorHeap(), GetSamplerAllocator().GetDescriptorHeap()};
	GetCommandList().list4->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);

	if (err)
	{
		ERROR_LOG("ShaderChain(D3D12): frame failed: {}", ShaderChain::DescribeAndFreeError(err));
		m_shader_chain_failed = true;
		return false;
	}
	return true;
}
```
`GetCommandList().list4` is a `wil::com_ptr_nothrow<ID3D12GraphicsCommandList4>` (`GSDevice12.h:36`), so `.get()` is the raw-pointer accessor. The output texture is left in `RenderTarget` state; PCSX2's `PresentRect` transitions it to `PixelShaderResource` through the normal `GSTexture12` state tracking because librashader does not alter PCSX2's tracked state.

- [ ] **Step 3: Build and validate (Windows x64, after Task 11)**

Set `Renderer = 15` (D3D12) with the Task 7 preset. Expected: CRT effect, crisp OSD, `ShaderChain(D3D12): loaded`, no debug-layer errors with "Use Debug Device". Confirm the HW renderer keeps drawing after the first shaded frame (a missing heap rebind shows as garbage or a device removed error), and that removing `dxcompiler.dll` yields the OSD error and an unshaded game rather than a crash.

- [ ] **Step 4: Commit**

```bash
git add pcsx2/GS/Renderers/DX12/GSDevice12.h pcsx2/GS/Renderers/DX12/GSDevice12.cpp
git commit -m "GS/DX12: Implement librashader shader chain

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 11: Windows deps build and MSBuild wiring

**Files:**
- Modify: `.github/workflows/scripts/windows/build-dependencies.bat` (version variables after line 92; build step after the shaderc `cd ..` and before `echo Cleaning up...`)
- Modify: `common/vsprops/LinkPCSX2Deps.props` (`DepsDLLs` item group)
- Modify: `common/vsprops/common.props:29` (`PreprocessorDefinitions`)
- Modify: `pcsx2/CMakeLists.txt:1303` (`DEPS_TO_COPY` for the Windows CMake install)

**Interfaces:**
- Consumes: Rust `cargo`/`rustup` on the Windows deps build host; Windows SDK (`%WindowsSdkDir%`, `%WindowsSDKVersion%` from `vcvars64.bat`).
- Produces: `deps\bin\librashader.dll`, `deps\bin\librashader.pdb`, `deps\bin\dxcompiler.dll`; both DLLs copied next to `pcsx2-qt.exe` by MSBuild and by the CMake install; `PCSX2_HAS_LIBRASHADER=1` defined for Windows MSBuild builds.

Windows-only: run and verify on a Windows x64 machine with Visual Studio, 7-Zip, Git for Windows and Rust installed.

- [ ] **Step 1: Add the librashader step to `build-dependencies.bat`**

After line 92 (`set SHADERC_SPIRVTOOLS=...`) add:
```bat
set LIBRASHADER=0.12.0
set LIBRASHADER_RUST=1.88
```
After the shaderc block's final `cd .. || goto error` and before `echo Cleaning up...` add:
```bat
echo Building librashader...
where cargo >nul 2>nul || (echo cargo not found on PATH; install Rust from https://rustup.rs && goto error)
where rustup >nul 2>nul || (echo rustup not found on PATH; install Rust from https://rustup.rs && goto error)
rustup toolchain install %LIBRASHADER_RUST% --profile minimal || goto error
rmdir /S /Q "librashader-%LIBRASHADER%" 2>nul
git clone --depth 1 --branch "librashader-v%LIBRASHADER%" https://github.com/SnowflakePowered/librashader.git "librashader-%LIBRASHADER%" || goto error
cd "librashader-%LIBRASHADER%" || goto error
rem Only the runtimes PCSX2 uses on Windows. Never enable runtime-d3d9 (it drags in D3DX9_43.dll).
rustup run %LIBRASHADER_RUST% cargo build -p librashader-capi --profile optimized --no-default-features --features runtime-vulkan,runtime-d3d11,runtime-d3d12 || goto error
copy /Y "target\optimized\librashader_capi.dll" "%INSTALLDIR%\bin\librashader.dll" || goto error
copy /Y "target\optimized\librashader_capi.pdb" "%INSTALLDIR%\bin\librashader.pdb"
copy /Y "include\librashader.h" "%INSTALLDIR%\include\librashader.h" || goto error
cd .. || goto error

echo Copying dxcompiler.dll for the librashader D3D12 runtime...
copy /Y "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxcompiler.dll" "%INSTALLDIR%\bin\dxcompiler.dll" || goto error
```
(`%WindowsSDKVersion%` ends with a backslash when set by `vcvars64.bat`, so the path above resolves to `...\bin\10.0.xxxxx.0\x64\dxcompiler.dll`.)

- [ ] **Step 2: MSBuild DLL copy and define**

In `common/vsprops/LinkPCSX2Deps.props`, after `<DepsDLLs Include="$(DepsBinDir)shaderc_shared.dll" />` add:
```xml
    <DepsDLLs Include="$(DepsBinDir)librashader.dll" />
    <DepsDLLs Include="$(DepsBinDir)dxcompiler.dll" />
```
(The existing `DepsListPDBs` target picks up `librashader.pdb` automatically.)

In `common/vsprops/common.props` line 29, prepend `PCSX2_HAS_LIBRASHADER=1;` to the `PreprocessorDefinitions` list so it reads:
```xml
<PreprocessorDefinitions>PCSX2_HAS_LIBRASHADER=1;__WIN32__;WIN32;_WINDOWS;WIN32_LEAN_AND_MEAN;NOMINMAX;_CRT_NONSTDC_NO_WARNINGS;_CRT_SECURE_NO_WARNINGS;_CRT_SECURE_NO_DEPRECATE;_SCL_SECURE_NO_WARNINGS;_HAS_EXCEPTIONS=0;WINVER=0x0A00;_WIN32_WINNT=0x0A00;%(PreprocessorDefinitions)</PreprocessorDefinitions>
```

- [ ] **Step 3: Windows CMake install copy**

In `pcsx2/CMakeLists.txt` line 1303, add `librashader.dll dxcompiler.dll` to `DEPS_TO_COPY`:
```cmake
		set(DEPS_TO_COPY freetype.dll harfbuzz.dll jpeg62.dll libpng16.dll libsharpyuv.dll libwebp.dll libwebpdemux.dll libwebpmux.dll lz4.dll SDL3.dll shaderc_shared.dll z.dll zstd.dll plutovg.dll plutosvg.dll ryml.dll librashader.dll dxcompiler.dll)
```

- [ ] **Step 4: Build the deps and PCSX2 on Windows and verify**

```bat
.github\workflows\scripts\windows\build-dependencies.bat
dir deps\bin\librashader.dll deps\bin\dxcompiler.dll
dumpbin /DEPENDENTS deps\bin\librashader.dll | findstr /I "d3dx9 d3dcompiler dxcompiler"
```
Expected: both DLLs present; `dumpbin` lists `d3dcompiler_47.dll` and `dxcompiler.dll` but **not** `D3DX9_43.dll`. Then open `PCSX2_qt.slnx`, build `Release Clang|x64` (or run the CI CMake configure from `windows_build_qt.yml:122-127`) and confirm `bin\librashader.dll` and `bin\dxcompiler.dll` exist beside `pcsx2-qt.exe`. Launch, and check `emulog.txt` for `librashader loaded from ... (ABI 2, API 5)`. Then run the Task 9 and Task 10 validation steps.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/scripts/windows/build-dependencies.bat common/vsprops/LinkPCSX2Deps.props common/vsprops/common.props pcsx2/CMakeLists.txt
git commit -m "Build: Build librashader 0.12.0 in Windows deps and copy it with dxcompiler.dll

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 12: Qt Post-Processing tab: Shader Chain group

**Files:**
- Modify: `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui` (new group box between `filtersLayout` and `verticalSpacer`; tab stops)
- Modify: `pcsx2-qt/Settings/GraphicsSettingsWidget.h` (slots and a helper)
- Modify: `pcsx2-qt/Settings/GraphicsSettingsWidget.cpp` (includes; bindings after line 223; help text)

**Interfaces:**
- Consumes: `ShaderPresets::Enumerate()` (Task 3), `ShaderChain::GetAvailability()` (Task 4), `EmuFolders::Shaders` (Task 2), `SettingWidgetBinder::BindWidgetToBoolSetting/BindWidgetToStringSetting`, `QtUtils::OpenURL`.
- Produces: UI widgets `shaderChainGroup`, `shaderChainEnabled`, `shaderChainPreset`, `shaderChainRefresh`, `shaderChainOpenFolder`, `shaderChainStatus`; slots `onShaderChainEnabledChanged()`, `onShaderChainRefreshClicked()`, `onShaderChainOpenFolderClicked()`; helper `populateShaderChainPresets()`.

No automated UI test; verified by hand in the settings dialog.

- [ ] **Step 1: Add the group box to the `.ui`**

Insert this `<item>` after the `filtersLayout` group's closing `</item>` and before the `verticalSpacer` item:
```xml
   <item>
    <widget class="QGroupBox" name="shaderChainGroup">
     <property name="title">
      <string>Shader Chain (librashader)</string>
     </property>
     <layout class="QGridLayout" name="gridLayout_shaderChain">
      <item row="0" column="0" colspan="4">
       <widget class="QCheckBox" name="shaderChainEnabled">
        <property name="text">
         <string>Enable Shader Chain</string>
        </property>
       </widget>
      </item>
      <item row="1" column="0">
       <widget class="QLabel" name="shaderChainPresetLabel">
        <property name="text">
         <string>Preset:</string>
        </property>
        <property name="buddy">
         <cstring>shaderChainPreset</cstring>
        </property>
       </widget>
      </item>
      <item row="1" column="1">
       <widget class="QComboBox" name="shaderChainPreset">
        <property name="sizePolicy">
         <sizepolicy hsizetype="Expanding" vsizetype="Fixed">
          <horstretch>1</horstretch>
          <verstretch>0</verstretch>
         </sizepolicy>
        </property>
       </widget>
      </item>
      <item row="1" column="2">
       <widget class="QPushButton" name="shaderChainRefresh">
        <property name="text">
         <string>Refresh</string>
        </property>
       </widget>
      </item>
      <item row="1" column="3">
       <widget class="QPushButton" name="shaderChainOpenFolder">
        <property name="text">
         <string>Open Folder...</string>
        </property>
       </widget>
      </item>
      <item row="2" column="0" colspan="4">
       <widget class="QLabel" name="shaderChainStatus">
        <property name="text">
         <string/>
        </property>
        <property name="wordWrap">
         <bool>true</bool>
        </property>
       </widget>
      </item>
     </layout>
    </widget>
   </item>
```
Append to `<tabstops>`:
```xml
  <tabstop>shaderChainEnabled</tabstop>
  <tabstop>shaderChainPreset</tabstop>
  <tabstop>shaderChainRefresh</tabstop>
  <tabstop>shaderChainOpenFolder</tabstop>
```

- [ ] **Step 2: Declare slots and helper in `GraphicsSettingsWidget.h`**

In `private Q_SLOTS:` after `void onShadeBoostChanged();` add:
```cpp
	void onShaderChainEnabledChanged();
	void onShaderChainRefreshClicked();
	void onShaderChainOpenFolderClicked();
```
In `private:` after `void populateUpscaleMultipliers(u32 max_upscale_multiplier);` add:
```cpp
	void populateShaderChainPresets();
```

- [ ] **Step 3: Bind and populate in `GraphicsSettingsWidget.cpp`**

Add includes after `#include "pcsx2/GS/GSUtil.h"`:
```cpp
#include "pcsx2/GS/ShaderChain/LibrashaderLoader.h"
#include "pcsx2/GS/ShaderChain/ShaderPresets.h"
#include "common/Path.h"
#include <QtCore/QUrl>
```
After `onShadeBoostChanged();` (line 223) add:
```cpp
	// Shader chain (librashader). The combobox stores the relative preset path as item data;
	// SettingAccessor<QComboBox>::getStringValue() prefers currentData() and setStringValue() uses findData().
	populateShaderChainPresets();
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_post.shaderChainEnabled, "EmuCore/GS", "ShaderChainEnabled", false);
	SettingWidgetBinder::BindWidgetToStringSetting(sif, m_post.shaderChainPreset, "EmuCore/GS", "ShaderChainPreset", "");
	connect(m_post.shaderChainEnabled, &QCheckBox::checkStateChanged, this, &GraphicsSettingsWidget::onShaderChainEnabledChanged);
	connect(m_post.shaderChainRefresh, &QPushButton::clicked, this, &GraphicsSettingsWidget::onShaderChainRefreshClicked);
	connect(m_post.shaderChainOpenFolder, &QPushButton::clicked, this, &GraphicsSettingsWidget::onShaderChainOpenFolderClicked);

	{
		const ShaderChain::Availability& avail = ShaderChain::GetAvailability();
		if (!avail.available)
		{
			m_post.shaderChainGroup->setEnabled(false);
			m_post.shaderChainGroup->setToolTip(QString::fromStdString(avail.reason));
			m_post.shaderChainStatus->setText(tr("Shader chain unavailable: %1").arg(QString::fromStdString(avail.reason)));
		}
		else
		{
			m_post.shaderChainStatus->setText(tr("Presets are loaded from %1. Place RetroArch slang shader packs in a shaders_slang subfolder.")
			                                     .arg(QString::fromStdString(EmuFolders::Shaders)));
		}
	}
	onShaderChainEnabledChanged();
```
Add the implementations next to `onShadeBoostChanged()`:
```cpp
void GraphicsSettingsWidget::populateShaderChainPresets()
{
	// Preserve the current value across repopulation; the binder re-applies it on rebuild.
	const QString current = m_post.shaderChainPreset->currentData().toString();
	QSignalBlocker blocker(m_post.shaderChainPreset);
	m_post.shaderChainPreset->clear();
	m_post.shaderChainPreset->addItem(tr("(None)"), QString());
	for (const std::string& preset : ShaderPresets::Enumerate())
	{
		const QString qpreset = QString::fromStdString(preset);
		m_post.shaderChainPreset->addItem(qpreset, qpreset);
	}
	const int index = m_post.shaderChainPreset->findData(current);
	m_post.shaderChainPreset->setCurrentIndex(index >= 0 ? index : 0);
}

void GraphicsSettingsWidget::onShaderChainEnabledChanged()
{
	const bool enabled = dialog()->getEffectiveBoolValue("EmuCore/GS", "ShaderChainEnabled", false);
	m_post.shaderChainPreset->setEnabled(enabled);
	m_post.shaderChainRefresh->setEnabled(enabled);
}

void GraphicsSettingsWidget::onShaderChainRefreshClicked()
{
	populateShaderChainPresets();
}

void GraphicsSettingsWidget::onShaderChainOpenFolderClicked()
{
	QtUtils::OpenURL(this, QUrl::fromLocalFile(QString::fromStdString(EmuFolders::Shaders)));
}
```
Add help text next to the existing `registerWidgetHelp(m_post.fxaa, ...)` call:
```cpp
	dialog()->registerWidgetHelp(m_post.shaderChainEnabled, tr("Enable Shader Chain"), tr("Unchecked"),
		tr("Applies a RetroArch slang shader preset (.slangp) to the displayed image using librashader. Replaces CAS and the TV Shader while active. "
		   "Screenshots, video captures and on-screen messages are not affected. Not supported by the OpenGL and Software renderers."));
	dialog()->registerWidgetHelp(m_post.shaderChainPreset, tr("Preset"), tr("(None)"),
		tr("Preset file to apply, relative to the Shaders folder. Presets that reference other shaders (for example the libretro shaders_slang pack) "
		   "must be installed with their directory structure intact."));
```
Per-game behaviour: `BindWidgetToStringSetting` inserts "Use Global Setting" as index 0 when a per-game interface is active, so `populateShaderChainPresets()` must run **before** the bind call (it does, above). Because `populateShaderChainPresets()` is also invoked by Refresh after binding, when `dialog()->isPerGameSettings()` is true it must re-insert the global-setting item: add at the start of `populateShaderChainPresets()`, after `clear()`:
```cpp
	if (dialog()->isPerGameSettings())
	{
		const std::string global_value = Host::GetBaseStringSettingValue("EmuCore/GS", "ShaderChainPreset", "");
		m_post.shaderChainPreset->addItem(tr("Use Global Setting [%1]").arg(global_value.empty() ? tr("(None)") : QString::fromStdString(global_value)));
	}
```
and when a per-game interface is active, treat `index < 0` as index 0 (the global item), which the code above already does.

- [ ] **Step 4: Build and verify by hand**

```bash
cd ~/work/pcsx2 && cmake --build build --target pcsx2-qt 2>&1 | grep -E " error"; cmake --build build --target pcsx2-qt 2>&1 | tail -1 && open build/bin/PCSX2.app
```
In Settings > Graphics > Post-Processing: the "Shader Chain (librashader)" group is enabled and lists presets from `~/Library/Application Support/PCSX2/shaders` (after placing `shaders_slang` there). Select `shaders_slang/crt/crt-geom.slangp`, enable, start a game: the effect appears within a frame without restarting. Open a per-game settings window: the combobox shows "Use Global Setting [...]" first. Rename `Contents/Frameworks/librashader.dylib` and relaunch: the group is disabled and the status shows the reason.

- [ ] **Step 5: Commit**

```bash
git add pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui pcsx2-qt/Settings/GraphicsSettingsWidget.h pcsx2-qt/Settings/GraphicsSettingsWidget.cpp
git commit -m "Qt: Add Shader Chain group to the Post-Processing tab

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 13: Shaders folder row in Folder settings

**Files:**
- Modify: `pcsx2-qt/Settings/FolderSettingsWidget.ui` (new group box after `coversGroup`)
- Modify: `pcsx2-qt/Settings/FolderSettingsWidget.cpp:17-25` (binding and help)

**Interfaces:**
- Consumes: `SettingWidgetBinder::BindWidgetToFolderSetting`, `EmuFolders::DataRoot`.
- Produces: widgets `shaders`, `shadersBrowse`, `shadersOpen`, `shadersReset`, `shadersLabel`; INI `Folders/Shaders`.

- [ ] **Step 1: Add the group box**

After the `coversGroup` `</item>` in `FolderSettingsWidget.ui` insert:
```xml
   <item>
    <widget class="QGroupBox" name="shadersGroup">
     <property name="title">
      <string>Shaders Directory</string>
     </property>
     <layout class="QGridLayout" name="gridLayout_shaders">
      <item row="1" column="0">
       <widget class="QLineEdit" name="shaders"/>
      </item>
      <item row="1" column="1">
       <widget class="QPushButton" name="shadersBrowse">
        <property name="text">
         <string>Browse...</string>
        </property>
       </widget>
      </item>
      <item row="1" column="2">
       <widget class="QPushButton" name="shadersOpen">
        <property name="text">
         <string>Open...</string>
        </property>
       </widget>
      </item>
      <item row="1" column="3">
       <widget class="QPushButton" name="shadersReset">
        <property name="text">
         <string>Reset</string>
        </property>
       </widget>
      </item>
      <item row="0" column="0" colspan="4">
       <widget class="QLabel" name="shadersLabel">
        <property name="text">
         <string>Used for storing RetroArch slang shader presets and shader packs for the post-processing shader chain.</string>
        </property>
        <property name="textInteractionFlags">
         <set>Qt::TextInteractionFlag::TextBrowserInteraction</set>
        </property>
        <property name="buddy">
         <cstring>shaders</cstring>
        </property>
       </widget>
      </item>
     </layout>
    </widget>
   </item>
```

- [ ] **Step 2: Bind and document**

In `FolderSettingsWidget.cpp`, after the `covers` binding line add:
```cpp
	SettingWidgetBinder::BindWidgetToFolderSetting(sif, m_ui.shaders, m_ui.shadersBrowse, m_ui.shadersOpen, m_ui.shadersReset, "Folders", "Shaders", Path::Combine(EmuFolders::DataRoot, "shaders"));
```
and after the `covers` help registration:
```cpp
	dialog()->registerWidgetHelp(m_ui.shaders, tr("Shaders Directory"), tr("Default"),
		tr("Location where shader presets (.slangp) and shader packs for the post-processing shader chain are stored."));
```

- [ ] **Step 3: Build and verify**

```bash
cd ~/work/pcsx2 && cmake --build build --target pcsx2-qt 2>&1 | tail -1 && open build/bin/PCSX2.app
```
Settings > Folders shows "Shaders Directory" with the default path; Browse to another folder, restart, and confirm the Post-Processing preset list reads from the new location; Reset restores the default.

- [ ] **Step 4: Commit**

```bash
git add pcsx2-qt/Settings/FolderSettingsWidget.ui pcsx2-qt/Settings/FolderSettingsWidget.cpp
git commit -m "Qt: Add Shaders directory to Folder settings

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 14: Acceptance matrix and spec follow-ups

**Files:**
- Modify: `docs/superpowers/specs/2026-09-12-librashader-shader-chain-design.md` (section 9 table filled in; section 6.1 updated if the Vulkan submit fallback was needed)

**Interfaces:** none (verification only).

- [ ] **Step 1: Install the test presets**

Under `<Shaders>/shaders_slang/` (extracted from `shaders_slang.zip`, no top-level folder) confirm these exist: `crt/crt-geom.slangp` (1 pass), `crt/crt-royale.slangp` (LUTs), and add the two third-party packs:
- satpixie: extract the release zip and copy `satpixie-crt-shader/RetroArch/shaders/shaders_slang/crt/satpixie-crt.slangp` to `<Shaders>/shaders_slang/crt/` and `.../crt/shaders/satpixie/` to `<Shaders>/shaders_slang/crt/shaders/satpixie/` (uses `PassFeedback`).
- RetroCrisis: extract `Retro.Crisis.GDV-NTSC.2026.08.20.zip.zip` so that `<Shaders>/shaders_slang/retro crisis/4K Flat/RC GDV-NTSC - PS2 - Clean.slangp` exists (18 passes, `#reference` + `../../../shaders_slang/...`). Delete the `__MACOSX` folder.

- [ ] **Step 2: Run the matrix and record results in the spec**

For each backend (Vulkan/Windows, Vulkan/macOS, Metal, D3D11, D3D12) and each preset (crt-geom, crt-royale, satpixie, RetroCrisis PS2 Clean): launch a game, confirm the effect renders, the game runs at full speed, and the OSD is crisp. Then per backend: switch presets via the settings dialog while running; toggle the chain off and on; switch renderer with the chain active; resize the window; take a screenshot (F8) and confirm it is unshaded; delete/rename the library and confirm normal launch with the UI group disabled. Fill the table in spec section 9 with pass/fail and notes.

- [ ] **Step 3: Record deviations**

If Task 7 needed the `ExecuteCommandBuffer(false)` fallback, or Task 10 needed changes to the heap rebind, update spec sections 6.1 / 6.4 to describe what shipped.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-09-12-librashader-shader-chain-design.md
git commit -m "Docs: Record shader chain phase 1 acceptance results

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```
