# Shader Parameters, Favourites and Hotkeys Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users tune the `#pragma parameter` values of the active librashader preset with live apply, persist them per preset (global or per game), cycle a favourites list with hotkeys, and show the active preset in the settings overlay.

**Architecture:** One new core module `pcsx2/GS/ShaderChain/ShaderChainParams.{h,cpp}` owns parameter enumeration (librashader preset parse, no GPU), `name=value` override parsing/formatting, pushing overrides into the existing `ShaderPresets::ParameterStore`, and favourites stepping. `VMManager::ApplySettings()` and three new GS hotkeys call it on the CPU thread; two new Qt dialogs call it on the UI thread. Backends are unchanged.

**Tech Stack:** C++20, librashader 0.12.0 C API (runtime loaded via `ShaderChain::Common()`), Qt 6 Widgets, PCSX2 `SettingsInterface` string lists, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-09-13-shader-parameters-design.md`

## Global Constraints

- Branch: `feature/shader-parameters` off `master` @ `179e1ed39`. Never build in the user's `build/` directory; the Mac build tree is `build-sc`.
- Settings keys are exactly: section `ShaderChainParams`, key = preset relative path (forward slashes), value = string list of `name=value`; `EmuCore/GS` / `ShaderChainFavorites` string list of relative preset paths (global only).
- Only parameters whose value differs from the preset default are stored. An empty list removes the key instead of writing an empty list.
- Values are formatted with `fmt::format("{}", value)` and parsed with `StringUtil::FromChars<float>`. Malformed entries are skipped with a `WARNING_LOG`; the last duplicate name wins.
- Hotkey names: `ToggleShaderChain`, `NextShaderPreset`, `PreviousShaderPreset`; category `Graphics`; OSD key `ShaderChainHotkey`; runtime-only (no INI writes).
- Overlay entry: `SC=<stem> ` in the hardware-renderer block of `DrawSettingsOverlay`, stem truncated to 32 characters plus `...` (ASCII; the ImGui overlay font is not guaranteed to have the ellipsis glyph, a deliberate deviation from the spec's `…`).
- Dialog copy: window title `Shader Parameters - <stem>`; footer `Reset All` (global) / `Use Global Settings` (per game); status texts `Could not load preset: <error>` and `This preset has no adjustable parameters.`; favourites tooltip `Favourites are shared by all games and can be edited in the global settings.`
- Slider: `N = min(round((max-min)/step), 10000)` positions; spin box decimals from step (1 gives 0, 0.5 gives 1, 0.01 gives 2, capped at 4). Parameters with `step <= 0` or `max <= min` get a spin box only.
- Dialog write debounce: 250 ms single-shot timer; `done()` flushes.
- All files carry the SPDX header `// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team` / `// SPDX-License-Identifier: GPL-3.0+`. Tabs for indentation, PCSX2 brace style.
- Commit trailer, exact text: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Tests: `cmake --build build-sc --target core_test` then run `build-sc/tests/ctest/core/core_test` (locate with `find build-sc -name core_test -type f -perm +111` if the path differs). Every task must leave the Mac build green: `cmake --build build-sc`.
- Windows verification (Task 6 only) uses the remote workflow in `docs/superpowers/plans/2026-09-12-librashader-shader-chain.md` ("Windows Remote Workflow").

## File Structure

| File | Responsibility |
|---|---|
| `pcsx2/GS/ShaderChain/ShaderChainParams.{h,cpp}` | Parameter enumeration, override parse/format, store push, favourites stepping |
| `pcsx2/GS/ShaderChain/LibrashaderLoader.cpp` | `PCSX2_LIBRASHADER_PATH` environment override for the library path (tests) |
| `pcsx2/VMManager.cpp` | Push overrides after every settings load |
| `pcsx2/GS/GS.cpp` | Three hotkeys |
| `pcsx2/ImGui/ImGuiOverlays.cpp` | `SC=` overlay entry |
| `pcsx2-qt/ShaderParametersDialog.{h,cpp,ui}` | Parameter editor |
| `pcsx2-qt/ShaderFavoritesDialog.{h,cpp,ui}` | Favourites editor |
| `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui`, `GraphicsSettingsWidget.{h,cpp}` | `Parameters...` and `Favorites...` buttons |
| `pcsx2/CMakeLists.txt`, `pcsx2/pcsx2.vcxproj(.filters)`, `pcsx2-qt/CMakeLists.txt`, `pcsx2-qt/pcsx2-qt.vcxproj(.filters)`, `tests/ctest/core/CMakeLists.txt` | Build registration |
| `tests/ctest/core/shader_chain_params_tests.cpp` | Unit tests |

---

### Task 1: Core module: overrides, store push, favourites stepping

**Files:**
- Create: `pcsx2/GS/ShaderChain/ShaderChainParams.h`
- Create: `pcsx2/GS/ShaderChain/ShaderChainParams.cpp`
- Create: `tests/ctest/core/shader_chain_params_tests.cpp`
- Modify: `pcsx2/CMakeLists.txt:507-508` (sources) and `:549-550` (headers)
- Modify: `pcsx2/pcsx2.vcxproj:343-344` (ClCompile) and `:785-786` (ClInclude)
- Modify: `pcsx2/pcsx2.vcxproj.filters:1085-1090` (ClCompile) and `:2012-2017` (ClInclude)
- Modify: `tests/ctest/core/CMakeLists.txt:1-9`

**Interfaces:**
- Consumes: `ShaderPresets::ParameterStore::ParamList` (`std::vector<std::pair<std::string, float>>`), `ShaderPresets::Params()`, `ShaderPresets::ResolvePresetPathIn(root, relative)` from `pcsx2/GS/ShaderChain/ShaderPresets.h`; `Host::GetStringListSetting(section, key)` from `pcsx2/Host.h`; `EmuFolders::Shaders` from `pcsx2/Config.h`.
- Produces (used by Tasks 2-5):
  - `struct ShaderChainParams::ParameterInfo { std::string name, description; float initial, minimum, maximum, step; }`
  - `using ShaderChainParams::ParamList = ShaderPresets::ParameterStore::ParamList;`
  - `ParamList ShaderChainParams::ParseOverrides(const std::vector<std::string>& entries);`
  - `std::vector<std::string> ShaderChainParams::FormatOverrides(const ParamList& params);`
  - `const char* ShaderChainParams::SettingsSection();` returns `"ShaderChainParams"`
  - `void ShaderChainParams::ApplyOverridesToStore(std::string_view preset_relative_path);`
  - `std::string ShaderChainParams::NextFavorite(const std::vector<std::string>& favorites, std::string_view current, bool forward);`
  - `std::string ShaderChainParams::NextFavoriteIn(const std::string& shaders_root, const std::vector<std::string>& favorites, std::string_view current, bool forward);`
  - `bool ShaderChainParams::EnumerateParameters(...)` is declared here but implemented in Task 2.

- [ ] **Step 1: Write the failing tests**

Create `tests/ctest/core/shader_chain_params_tests.cpp`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderChainParams.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include <gtest/gtest.h>
#include <filesystem>
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
					"pcsx2_shader_params_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempTree() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }
		void file(const char* rel, const char* contents = "#reference nothing\n")
		{
			const std::string full = Path::Combine(m_root, rel);
			FileSystem::CreateDirectoryPath(std::string(Path::GetDirectory(full)).c_str(), true);
			FileSystem::WriteStringToFile(full.c_str(), contents);
		}

	private:
		std::string m_root;
	};
} // namespace

TEST(ShaderChainParams, ParseOverridesRoundTripsThroughFormat)
{
	const ShaderChainParams::ParamList in = {{"GAMMA", 2.4f}, {"OFFSET", -0.25f}, {"BIG", 12345.5f}};
	const std::vector<std::string> entries = ShaderChainParams::FormatOverrides(in);
	ASSERT_EQ(entries.size(), 3u);
	EXPECT_EQ(entries[0], "GAMMA=2.4");
	EXPECT_EQ(entries[1], "OFFSET=-0.25");
	EXPECT_EQ(entries[2], "BIG=12345.5");

	const ShaderChainParams::ParamList out = ShaderChainParams::ParseOverrides(entries);
	ASSERT_EQ(out.size(), 3u);
	EXPECT_EQ(out[0].first, "GAMMA");
	EXPECT_FLOAT_EQ(out[0].second, 2.4f);
	EXPECT_EQ(out[1].first, "OFFSET");
	EXPECT_FLOAT_EQ(out[1].second, -0.25f);
	EXPECT_EQ(out[2].first, "BIG");
	EXPECT_FLOAT_EQ(out[2].second, 12345.5f);
}

TEST(ShaderChainParams, ParseOverridesSkipsMalformedEntries)
{
	const ShaderChainParams::ParamList out = ShaderChainParams::ParseOverrides({
		"no_equals_sign",
		"=1.0",
		"NOT_A_NUMBER=abc",
		"EMPTY_VALUE=",
		" SPACED = 3 ",
		"GOOD=1e-2",
	});
	ASSERT_EQ(out.size(), 2u);
	EXPECT_EQ(out[0].first, "SPACED");
	EXPECT_FLOAT_EQ(out[0].second, 3.0f);
	EXPECT_EQ(out[1].first, "GOOD");
	EXPECT_FLOAT_EQ(out[1].second, 0.01f);
}

TEST(ShaderChainParams, ParseOverridesLastDuplicateWins)
{
	const ShaderChainParams::ParamList out = ShaderChainParams::ParseOverrides({"A=1", "B=2", "A=3"});
	ASSERT_EQ(out.size(), 2u);
	EXPECT_EQ(out[0].first, "A");
	EXPECT_FLOAT_EQ(out[0].second, 3.0f);
	EXPECT_EQ(out[1].first, "B");
	EXPECT_FLOAT_EQ(out[1].second, 2.0f);
}

TEST(ShaderChainParams, FormatOverridesOfEmptyListIsEmpty)
{
	EXPECT_TRUE(ShaderChainParams::FormatOverrides({}).empty());
	EXPECT_STREQ(ShaderChainParams::SettingsSection(), "ShaderChainParams");
}

TEST(ShaderChainParams, NextFavoriteStepsAndWraps)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("a.slangp");
	t.file("sub/b.slangp");
	t.file("c.slangp");
	const std::vector<std::string> favs = {"a.slangp", "sub/b.slangp", "c.slangp"};

	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "a.slangp", true), "sub/b.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "sub/b.slangp", true), "c.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "c.slangp", true), "a.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "a.slangp", false), "c.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "c.slangp", false), "sub/b.slangp");
}

TEST(ShaderChainParams, NextFavoriteWhenCurrentNotListed)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("a.slangp");
	t.file("c.slangp");
	const std::vector<std::string> favs = {"a.slangp", "c.slangp"};

	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "", true), "a.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "other.slangp", true), "a.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "other.slangp", false), "c.slangp");
}

TEST(ShaderChainParams, NextFavoriteSkipsMissingFiles)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("a.slangp");
	t.file("c.slangp");
	const std::vector<std::string> favs = {"a.slangp", "missing.slangp", "c.slangp"};

	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "a.slangp", true), "c.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "c.slangp", false), "a.slangp");
	// Only the current entry exists: stepping lands on it again rather than returning empty.
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), {"a.slangp", "missing.slangp"}, "a.slangp", true), "a.slangp");
}

TEST(ShaderChainParams, NextFavoriteEmptyOrAllMissingReturnsEmpty)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	EXPECT_TRUE(ShaderChainParams::NextFavoriteIn(t.root(), {}, "a.slangp", true).empty());
	EXPECT_TRUE(ShaderChainParams::NextFavoriteIn(t.root(), {"x.slangp", "y.slangp"}, "", true).empty());
	EXPECT_TRUE(ShaderChainParams::NextFavoriteIn(t.root(), {"../escape.slangp"}, "", true).empty());
}
```

Register it in `tests/ctest/core/CMakeLists.txt` by adding `shader_chain_params_tests.cpp` after `shader_presets_tests.cpp`:

```cmake
add_pcsx2_test(core_test
	patch_tests.cpp
	shader_presets_tests.cpp
	shader_chain_params_tests.cpp
	librashader_loader_tests.cpp
	shader_packs_tests.cpp
	shader_pack_archive_tests.cpp
	MockMemoryInterface.h
	StubHost.cpp
)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build-sc --target core_test 2>&1 | tail -5`
Expected: compile error, `GS/ShaderChain/ShaderChainParams.h` file not found.

- [ ] **Step 3: Write the header**

Create `pcsx2/GS/ShaderChain/ShaderChainParams.h`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/ShaderChain/ShaderPresets.h"

#include <string>
#include <string_view>
#include <vector>

class Error;

/// User overrides for a preset's #pragma parameters, their INI representation, and favourites stepping.
/// No GPU code here; everything is safe on the CPU and UI threads.
namespace ShaderChainParams
{
	struct ParameterInfo
	{
		std::string name;
		std::string description;
		float initial = 0.0f;
		float minimum = 0.0f;
		float maximum = 0.0f;
		float step = 0.0f;
	};

	using ParamList = ShaderPresets::ParameterStore::ParamList;

	/// Loads the preset with librashader (no runtime, no GPU) and lists its #pragma parameters in
	/// preset order. Returns false with a non-empty error when the library is unavailable or the
	/// preset fails to parse.
	bool EnumerateParameters(const std::string& absolute_preset_path, std::vector<ParameterInfo>* out, Error* error);

	/// "name=value" entries -> list. Malformed entries are skipped with a warning; the last
	/// duplicate name wins and keeps the position of its first occurrence.
	ParamList ParseOverrides(const std::vector<std::string>& entries);
	std::vector<std::string> FormatOverrides(const ParamList& params);

	/// INI section holding one string-list key per preset (key = preset relative path).
	const char* SettingsSection();

	/// Reads the layered override list for the preset and pushes it into ShaderPresets::Params().
	/// A missing list pushes an empty ParamList, which resets the chain to its defaults.
	void ApplyOverridesToStore(std::string_view preset_relative_path);

	/// Next (forward) or previous entry of `favorites` relative to `current`, wrapping around and
	/// skipping entries whose file does not exist under the shaders root. If `current` is not in
	/// the list, forward returns the first existing entry and backward the last. Empty if none exist.
	std::string NextFavorite(const std::vector<std::string>& favorites, std::string_view current, bool forward);
	std::string NextFavoriteIn(const std::string& shaders_root, const std::vector<std::string>& favorites,
		std::string_view current, bool forward);
} // namespace ShaderChainParams
```

- [ ] **Step 4: Write the implementation (without `EnumerateParameters`, which Task 2 adds)**

Create `pcsx2/GS/ShaderChain/ShaderChainParams.cpp`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderChainParams.h"
#include "Config.h"
#include "Host.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/StringUtil.h"

#include "fmt/format.h"
#include "fmt/ranges.h" // fmt::join

#include <algorithm>

namespace
{
	std::string_view Trim(std::string_view s)
	{
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
			s.remove_prefix(1);
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
			s.remove_suffix(1);
		return s;
	}
} // namespace

const char* ShaderChainParams::SettingsSection()
{
	return "ShaderChainParams";
}

ShaderChainParams::ParamList ShaderChainParams::ParseOverrides(const std::vector<std::string>& entries)
{
	ParamList result;
	for (const std::string& entry : entries)
	{
		const size_t eq = entry.find('=');
		if (eq == std::string::npos)
		{
			WARNING_LOG("ShaderChainParams: ignoring malformed override '{}' (no '=').", entry);
			continue;
		}

		const std::string_view name = Trim(std::string_view(entry).substr(0, eq));
		const std::string_view value_str = Trim(std::string_view(entry).substr(eq + 1));
		const std::optional<float> value = StringUtil::FromChars<float>(value_str);
		if (name.empty() || !value.has_value())
		{
			WARNING_LOG("ShaderChainParams: ignoring malformed override '{}'.", entry);
			continue;
		}

		const auto existing = std::find_if(result.begin(), result.end(),
			[&name](const auto& p) { return p.first == name; });
		if (existing != result.end())
			existing->second = value.value();
		else
			result.emplace_back(std::string(name), value.value());
	}
	return result;
}

std::vector<std::string> ShaderChainParams::FormatOverrides(const ParamList& params)
{
	std::vector<std::string> entries;
	entries.reserve(params.size());
	for (const auto& [name, value] : params)
		entries.push_back(fmt::format("{}={}", name, value));
	return entries;
}

void ShaderChainParams::ApplyOverridesToStore(std::string_view preset_relative_path)
{
	std::string preset(preset_relative_path);
	ParamList params;
	if (!preset.empty())
		params = ParseOverrides(Host::GetStringListSetting(SettingsSection(), preset.c_str()));
	ShaderPresets::Params().Set(std::move(preset), std::move(params));
}

std::string ShaderChainParams::NextFavoriteIn(const std::string& shaders_root, const std::vector<std::string>& favorites,
	std::string_view current, bool forward)
{
	if (favorites.empty())
		return {};

	const auto exists = [&shaders_root](const std::string& rel) {
		const std::string full = ShaderPresets::ResolvePresetPathIn(shaders_root, rel);
		return !full.empty() && FileSystem::FileExists(full.c_str());
	};

	const s32 count = static_cast<s32>(favorites.size());
	const auto it = std::find(favorites.begin(), favorites.end(), current);
	// Not listed: start just outside the list so the first step lands on the first/last entry.
	const s32 start = (it != favorites.end()) ? static_cast<s32>(it - favorites.begin()) : (forward ? -1 : count);
	const s32 dir = forward ? 1 : -1;

	std::vector<std::string> skipped;
	for (s32 i = 1; i <= count; i++)
	{
		const s32 idx = ((start + dir * i) % count + count) % count;
		if (exists(favorites[idx]))
		{
			if (!skipped.empty())
				WARNING_LOG("ShaderChain: skipped missing favourite presets: {}", fmt::join(skipped, ", "));
			return favorites[idx];
		}
		skipped.push_back(favorites[idx]);
	}

	if (!skipped.empty())
		WARNING_LOG("ShaderChain: no favourite preset exists on disk: {}", fmt::join(skipped, ", "));
	return {};
}

std::string ShaderChainParams::NextFavorite(const std::vector<std::string>& favorites, std::string_view current, bool forward)
{
	return NextFavoriteIn(EmuFolders::Shaders, favorites, current, forward);
}
```

`fmt/ranges.h` exists at `3rdparty/fmt/include/fmt/ranges.h`. `s32` comes from `common/Pcsx2Defs.h` via `ShaderPresets.h`.

- [ ] **Step 5: Register the sources**

`pcsx2/CMakeLists.txt`: after line `GS/ShaderChain/ShaderPresets.cpp` add `GS/ShaderChain/ShaderChainParams.cpp`; after `GS/ShaderChain/ShaderPresets.h` add `GS/ShaderChain/ShaderChainParams.h`.

`pcsx2/pcsx2.vcxproj`: after `<ClCompile Include="GS\ShaderChain\ShaderPresets.cpp" />` add `<ClCompile Include="GS\ShaderChain\ShaderChainParams.cpp" />`; after `<ClInclude Include="GS\ShaderChain\ShaderPresets.h" />` add `<ClInclude Include="GS\ShaderChain\ShaderChainParams.h" />`.

`pcsx2/pcsx2.vcxproj.filters`: after the `ShaderPresets.cpp` ClCompile block add

```xml
    <ClCompile Include="GS\ShaderChain\ShaderChainParams.cpp">
      <Filter>System\Ps2\GS\ShaderChain</Filter>
    </ClCompile>
```

and after the `ShaderPresets.h` ClInclude block add

```xml
    <ClInclude Include="GS\ShaderChain\ShaderChainParams.h">
      <Filter>System\Ps2\GS\ShaderChain</Filter>
    </ClInclude>
```

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build-sc --target core_test 2>&1 | tail -3 && build-sc/tests/ctest/core/core_test --gtest_filter='ShaderChainParams*'`
Expected: 8 tests PASS. (`EnumerateParameters` is declared but not yet defined; nothing references it yet, so the link succeeds.)

- [ ] **Step 7: Full Mac build**

Run: `cmake --build build-sc 2>&1 | tail -3`
Expected: no errors.

- [ ] **Step 8: Commit**

```bash
git add pcsx2/GS/ShaderChain/ShaderChainParams.h pcsx2/GS/ShaderChain/ShaderChainParams.cpp \
  tests/ctest/core/shader_chain_params_tests.cpp tests/ctest/core/CMakeLists.txt \
  pcsx2/CMakeLists.txt pcsx2/pcsx2.vcxproj pcsx2/pcsx2.vcxproj.filters
git commit -m "GS/ShaderChain: Add ShaderChainParams overrides and favourites helpers

- name=value override parsing/formatting for the ShaderChainParams INI section
- ApplyOverridesToStore() pushes a preset's persisted overrides into the parameter store
- NextFavorite() steps through the favourites list, wrapping and skipping missing files
- Unit tests for parsing and stepping

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Parameter enumeration through librashader

**Files:**
- Modify: `pcsx2/GS/ShaderChain/ShaderChainParams.cpp` (add `EnumerateParameters`)
- Modify: `pcsx2/GS/ShaderChain/LibrashaderLoader.cpp:105-120` (`GetDefaultLibraryPath`: environment override)
- Modify: `pcsx2/GS/ShaderChain/LibrashaderLoader.h:50-51` (doc comment for the override)
- Modify: `tests/ctest/core/shader_chain_params_tests.cpp` (enumeration tests)
- Modify: `tests/ctest/core/librashader_loader_tests.cpp` (override test)

**Interfaces:**
- Consumes: `ShaderChain::GetAvailability()`, `ShaderChain::Common()` (`preset_ctx_create`, `preset_ctx_set_core_name`, `preset_create_with_options`, `preset_get_runtime_params`, `preset_free_runtime_params`, `preset_free`, `preset_ctx_free`), `ShaderChain::DescribeAndFreeError(libra_error_t)` from `LibrashaderLoader.h`; `libra_preset_opt_t`, `libra_preset_param_list_t`, `LIBRASHADER_CURRENT_VERSION` from `librashader.h`; `Error::SetString`, `Error::SetStringView` from `common/Error.h`.
- Produces: `bool ShaderChainParams::EnumerateParameters(const std::string& absolute_preset_path, std::vector<ParameterInfo>* out, Error* error)` (declared in Task 1). Environment variable `PCSX2_LIBRASHADER_PATH` overrides the library location for `ShaderChain::GetDefaultLibraryPath()`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/ctest/core/shader_chain_params_tests.cpp` (add `#include "GS/ShaderChain/LibrashaderLoader.h"` and `#include "common/Error.h"` at the top):

```cpp
namespace
{
	// Minimal preset + shader pair. Enumeration only preprocesses the shader (no compilation), but
	// keep the GLSL valid so the fixture also loads in a real chain.
	constexpr const char* FIXTURE_SLANGP = "shaders = 1\nshader0 = params.slang\n";
	constexpr const char* FIXTURE_SLANG =
		"#version 450\n"
		"#pragma parameter TEST_GAIN \"Test gain\" 1.0 0.0 2.0 0.1\n"
		"#pragma parameter TEST_FLAG \"Test flag\" 0.0 0.0 1.0 1.0\n"
		"layout(push_constant) uniform Push { float TEST_GAIN; float TEST_FLAG; } params;\n"
		"layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;\n"
		"#pragma stage vertex\n"
		"layout(location = 0) in vec4 Position;\n"
		"layout(location = 1) in vec2 TexCoord;\n"
		"layout(location = 0) out vec2 vTexCoord;\n"
		"void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }\n"
		"#pragma stage fragment\n"
		"layout(location = 0) in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 FragColor;\n"
		"layout(set = 0, binding = 2) uniform sampler2D Source;\n"
		"void main() { FragColor = texture(Source, vTexCoord) * params.TEST_GAIN; }\n";
} // namespace

TEST(ShaderChainParams, EnumerateParametersReadsPragmaParameters)
{
	if (!ShaderChain::GetAvailability().available)
		GTEST_SKIP() << "librashader not loadable: " << ShaderChain::GetAvailability().reason
					 << " (set PCSX2_LIBRASHADER_PATH to run this test)";

	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("fixture.slangp", FIXTURE_SLANGP);
	t.file("params.slang", FIXTURE_SLANG);

	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	ASSERT_TRUE(ShaderChainParams::EnumerateParameters(Path::Combine(t.root(), "fixture.slangp"), &params, &error))
		<< error.GetDescription();
	ASSERT_EQ(params.size(), 2u);
	EXPECT_EQ(params[0].name, "TEST_GAIN");
	EXPECT_EQ(params[0].description, "Test gain");
	EXPECT_FLOAT_EQ(params[0].initial, 1.0f);
	EXPECT_FLOAT_EQ(params[0].minimum, 0.0f);
	EXPECT_FLOAT_EQ(params[0].maximum, 2.0f);
	EXPECT_FLOAT_EQ(params[0].step, 0.1f);
	EXPECT_EQ(params[1].name, "TEST_FLAG");
	EXPECT_FLOAT_EQ(params[1].step, 1.0f);
}

TEST(ShaderChainParams, EnumerateParametersFailsForMissingShader)
{
	if (!ShaderChain::GetAvailability().available)
		GTEST_SKIP() << "librashader not loadable";

	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("broken.slangp", "shaders = 1\nshader0 = does_not_exist.slang\n");

	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	EXPECT_FALSE(ShaderChainParams::EnumerateParameters(Path::Combine(t.root(), "broken.slangp"), &params, &error));
	EXPECT_FALSE(error.GetDescription().empty());
	EXPECT_TRUE(params.empty());
}

TEST(ShaderChainParams, EnumerateParametersFailsForMissingPreset)
{
	if (!ShaderChain::GetAvailability().available)
		GTEST_SKIP() << "librashader not loadable";

	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	EXPECT_FALSE(ShaderChainParams::EnumerateParameters("/definitely/not/here.slangp", &params, &error));
	EXPECT_FALSE(error.GetDescription().empty());
}
```

Append to `tests/ctest/core/librashader_loader_tests.cpp` (add `#include <cstdlib>`; on Windows `setenv` does not exist, so use the helper below):

```cpp
namespace
{
	void SetEnvVar(const char* name, const char* value)
	{
#ifdef _WIN32
		_putenv_s(name, value);
#else
		if (value[0] == '\0')
			unsetenv(name);
		else
			setenv(name, value, 1);
#endif
	}
} // namespace

TEST(LibrashaderLoader, EnvironmentOverrideWinsOverDefaultPath)
{
	const char* previous = std::getenv("PCSX2_LIBRASHADER_PATH");
	const std::string saved = previous ? previous : "";

	SetEnvVar("PCSX2_LIBRASHADER_PATH", "/tmp/override/librashader-test.dylib");
	EXPECT_EQ(ShaderChain::GetDefaultLibraryPath(), "/tmp/override/librashader-test.dylib");

	SetEnvVar("PCSX2_LIBRASHADER_PATH", saved.c_str());
	if (saved.empty())
		EXPECT_NE(ShaderChain::GetDefaultLibraryPath(), "/tmp/override/librashader-test.dylib");
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build-sc --target core_test 2>&1 | grep -E "error|undefined" | head -5`
Expected: link error, undefined symbol `ShaderChainParams::EnumerateParameters`.

- [ ] **Step 3: Add the environment override to the loader**

In `pcsx2/GS/ShaderChain/LibrashaderLoader.cpp`, at the top of `ShaderChain::GetDefaultLibraryPath()` (before the `#ifdef _WIN32`), add:

```cpp
	// Tests and developers can point at a library outside the app bundle / exe directory.
	if (const char* env = std::getenv("PCSX2_LIBRASHADER_PATH"); env && env[0] != '\0')
		return env;
```

Add `#include <cstdlib>` next to `#include <mutex>`. In `LibrashaderLoader.h`, extend the `GetDefaultLibraryPath` comment:

```cpp
	/// Platform default location: next to the executable (Windows) or Contents/Frameworks (macOS bundle).
	/// The PCSX2_LIBRASHADER_PATH environment variable overrides both.
```

- [ ] **Step 4: Implement `EnumerateParameters`**

Append to `pcsx2/GS/ShaderChain/ShaderChainParams.cpp` (add `#include "GS/ShaderChain/LibrashaderLoader.h"` and `#include "common/Error.h"` to the includes):

```cpp
bool ShaderChainParams::EnumerateParameters(const std::string& absolute_preset_path, std::vector<ParameterInfo>* out, Error* error)
{
	out->clear();

	const ShaderChain::Availability& avail = ShaderChain::GetAvailability();
	if (!avail.available)
	{
		Error::SetStringView(error, avail.reason);
		return false;
	}

	const ShaderChain::CommonFunctions& c = ShaderChain::Common();
	libra_preset_ctx_t ctx = nullptr;
	libra_shader_preset_t preset = nullptr;
	// The context is only honoured (and only freed by librashader) when options are passed.
	libra_preset_opt_t popt = {};
	popt.version = LIBRASHADER_CURRENT_VERSION;

	libra_error_t err = c.preset_ctx_create(&ctx);
	if (!err) err = c.preset_ctx_set_core_name(&ctx, "PCSX2");
	if (!err) err = c.preset_create_with_options(absolute_preset_path.c_str(), &ctx, &popt, &preset);
	if (err)
	{
		Error::SetString(error, ShaderChain::DescribeAndFreeError(err));
		if (ctx) c.preset_ctx_free(&ctx);
		return false;
	}
	// libra_preset_create_with_options invalidates the context on success (header contract), so
	// only the failure path above frees it. Mirrors GSDeviceMTL::EnsureShaderChain.

	libra_preset_param_list_t list = {};
	err = c.preset_get_runtime_params(&preset, &list);
	if (err)
	{
		Error::SetString(error, ShaderChain::DescribeAndFreeError(err));
		c.preset_free(&preset);
		return false;
	}

	out->reserve(static_cast<size_t>(list.length));
	for (u64 i = 0; i < list.length; i++)
	{
		const libra_preset_param_t& p = list.parameters[i];
		ParameterInfo info;
		info.name = p.name ? p.name : "";
		info.description = p.description ? p.description : "";
		info.initial = p.initial;
		info.minimum = p.minimum;
		info.maximum = p.maximum;
		info.step = p.step;
		out->push_back(std::move(info));
	}

	if (libra_error_t ferr = c.preset_free_runtime_params(list))
		WARNING_LOG("ShaderChainParams: freeing runtime params failed: {}", ShaderChain::DescribeAndFreeError(ferr));
	c.preset_free(&preset);
	return true;
}
```

`Error::SetString(Error*, std::string)` and `Error::SetStringView(Error*, std::string_view)` are the static null-safe overloads in `common/Error.h:74-75`.

Context ownership: the header for `libra_preset_create_with_options` says "If `context` is provided, it is immediately invalidated and must be recreated after the preset is created", so the context is freed only when creation fails (and only if the handle is still non-null), exactly as `GSDeviceMTL::EnsureShaderChain` does at `pcsx2/GS/Renderers/Metal/GSDeviceMTL.mm:2893-2912`.

- [ ] **Step 5: Build and run the tests with the library available**

Run:

```bash
cmake --build build-sc --target core_test 2>&1 | tail -3
PCSX2_LIBRASHADER_PATH=$HOME/deps/lib/librashader.dylib build-sc/tests/ctest/core/core_test --gtest_filter='ShaderChainParams*:LibrashaderLoader*'
build-sc/tests/ctest/core/core_test --gtest_filter='ShaderChainParams.Enumerate*'
```

Expected: first run, all `ShaderChainParams` (11) and `LibrashaderLoader` (5) tests PASS, none skipped; second run, the three `Enumerate*` tests are SKIPPED (library not found without the override).

- [ ] **Step 6: Full Mac build**

Run: `cmake --build build-sc 2>&1 | tail -3`
Expected: no errors.

- [ ] **Step 7: Commit**

```bash
git add pcsx2/GS/ShaderChain/ShaderChainParams.cpp pcsx2/GS/ShaderChain/LibrashaderLoader.cpp \
  pcsx2/GS/ShaderChain/LibrashaderLoader.h tests/ctest/core/shader_chain_params_tests.cpp \
  tests/ctest/core/librashader_loader_tests.cpp
git commit -m "GS/ShaderChain: Enumerate preset parameters through librashader

- EnumerateParameters() parses a .slangp with the runtime-independent preset API and
  returns name/description/initial/min/max/step for each #pragma parameter
- PCSX2_LIBRASHADER_PATH environment override for the library path so unit tests can
  load the library outside the app bundle
- Fixture-based enumeration tests, skipped when the library is not loadable

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Apply flow, hotkeys and overlay entry

**Files:**
- Modify: `pcsx2/VMManager.cpp:755-775` (`VMManager::ApplySettings`) and its include block (`:14-16`)
- Modify: `pcsx2/GS/GS.cpp:1347-1371` (insert after the `CycleTVShader` entry) and its include block (`:4-24`)
- Modify: `pcsx2/ImGui/ImGuiOverlays.cpp:940-990` (hardware-renderer block of `DrawSettingsOverlay`)

**Interfaces:**
- Consumes: `ShaderChainParams::ApplyOverridesToStore(std::string_view)`, `ShaderChainParams::NextFavorite(favorites, current, forward)` from Task 1; `Host::GetStringListSetting`, `Host::AddKeyedOSDMessage(key, message, duration)`, `Host::OSD_QUICK_DURATION`, `TRANSLATE_STR`, `TRANSLATE_FS`, `TRANSLATE_NOOP`; `EmuConfig.GS.ShaderChainEnabled` (bitfield), `EmuConfig.GS.ShaderChainPreset` (`std::string`), `GSConfig` (GS-thread copy), `MTGS::RunOnGSThread`; `Path::GetFileTitle(std::string_view)`.
- Produces: hotkeys `ToggleShaderChain`, `NextShaderPreset`, `PreviousShaderPreset` visible in Settings > Hotkeys > Graphics; overlay token `SC=<stem>`.

This task has no unit-testable surface (hotkey lambdas and ImGui text); TDD is skipped deliberately. Verification is a clean build plus the manual checks in Task 6.

- [ ] **Step 1: Push overrides after every settings load**

In `pcsx2/VMManager.cpp`, add `#include "GS/ShaderChain/ShaderChainParams.h"` after `#include "GS/Renderers/HW/GSTextureReplacements.h"`. In `VMManager::ApplySettings()` change the tail to:

```cpp
	LoadSettings();
	// Parameter overrides are not part of Pcsx2Config, so push them explicitly whenever settings
	// (global or per-game layer) are reloaded. The GS thread re-applies them on the next frame.
	ShaderChainParams::ApplyOverridesToStore(EmuConfig.GS.ShaderChainPreset);
	CheckForConfigChanges(old_config);
```

- [ ] **Step 2: Add the hotkeys**

In `pcsx2/GS/GS.cpp`, add `#include "GS/ShaderChain/ShaderChainParams.h"` after `#include "GS/Renderers/HW/GSTextureReplacements.h"`. Above the `BEGIN_HOTKEY_LIST(g_gs_hotkeys)` line (`pcsx2/GS/GS.cpp:1208`), add:

```cpp
static void HotkeyCycleShaderPreset(bool forward)
{
	const std::vector<std::string> favorites = Host::GetStringListSetting("EmuCore/GS", "ShaderChainFavorites");
	const std::string preset = ShaderChainParams::NextFavorite(favorites, EmuConfig.GS.ShaderChainPreset, forward);
	if (preset.empty())
	{
		Host::AddKeyedOSDMessage("ShaderChainHotkey",
			TRANSLATE_STR("Hotkeys", "No shader presets in favourites list."), Host::OSD_QUICK_DURATION);
		return;
	}

	Host::AddKeyedOSDMessage("ShaderChainHotkey",
		fmt::format(TRANSLATE_FS("Hotkeys", "Shader preset: {}."), Path::GetFileTitle(preset)), Host::OSD_QUICK_DURATION);

	// Runtime-only, like the other graphics hotkeys: nothing is written to the INI.
	EmuConfig.GS.ShaderChainPreset = preset;
	EmuConfig.GS.ShaderChainEnabled = true;
	MTGS::RunOnGSThread([preset]() {
		GSConfig.ShaderChainPreset = preset;
		GSConfig.ShaderChainEnabled = true;
	});
	ShaderChainParams::ApplyOverridesToStore(preset);
}
```

Insert after the `CycleTVShader` entry (immediately before `{"CycleBlendingAccuracy", ...`):

```cpp
	{"ToggleShaderChain", TRANSLATE_NOOP("Hotkeys", "Graphics"), TRANSLATE_NOOP("Hotkeys", "Toggle Shader Chain"),
		[](s32 pressed) {
			if (pressed)
				return;

			const bool enabled = !EmuConfig.GS.ShaderChainEnabled;
			Host::AddKeyedOSDMessage("ShaderChainHotkey",
				enabled ? TRANSLATE_STR("Hotkeys", "Shader chain enabled.") : TRANSLATE_STR("Hotkeys", "Shader chain disabled."),
				Host::OSD_QUICK_DURATION);

			EmuConfig.GS.ShaderChainEnabled = enabled;
			MTGS::RunOnGSThread([enabled]() { GSConfig.ShaderChainEnabled = enabled; });
		}},
	{"NextShaderPreset", TRANSLATE_NOOP("Hotkeys", "Graphics"), TRANSLATE_NOOP("Hotkeys", "Next Shader Preset"),
		[](s32 pressed) {
			if (pressed)
				return;
			HotkeyCycleShaderPreset(true);
		}},
	{"PreviousShaderPreset", TRANSLATE_NOOP("Hotkeys", "Graphics"), TRANSLATE_NOOP("Hotkeys", "Previous Shader Preset"),
		[](s32 pressed) {
			if (pressed)
				return;
			HotkeyCycleShaderPreset(false);
		}},
```

`fmt/format.h`, `common/Path.h` and `Host.h` are already included in `GS.cpp`.

- [ ] **Step 3: Add the overlay token**

In `pcsx2/ImGui/ImGuiOverlays.cpp`, inside `DrawSettingsOverlay`'s `if (GSIsHardwareRenderer())` block, after the `if (GSConfig.HWROVBarriersVK) APPEND("RBVK ");` lines, add:

```cpp
		if (GSConfig.ShaderChainEnabled && !GSConfig.ShaderChainPreset.empty())
		{
			constexpr size_t MAX_STEM = 32;
			const std::string_view stem = Path::GetFileTitle(GSConfig.ShaderChainPreset);
			if (stem.size() > MAX_STEM)
				APPEND("SC={}... ", stem.substr(0, MAX_STEM));
			else
				APPEND("SC={} ", stem);
		}
```

`common/Path.h` is already included in `ImGuiOverlays.cpp`. Use the ASCII `...` rather than the Unicode ellipsis: no overlay in `pcsx2/ImGui/` uses `…`, so the glyph may be missing from the atlas.

- [ ] **Step 4: Build**

Run: `cmake --build build-sc 2>&1 | tail -3`
Expected: no errors. Also run `build-sc/tests/ctest/core/core_test --gtest_filter='ShaderChainParams*'` to confirm the module still links (8 pass, 3 skipped).

- [ ] **Step 5: Smoke check the hotkey list**

Run: `grep -n "ToggleShaderChain\|NextShaderPreset\|PreviousShaderPreset" pcsx2/GS/GS.cpp | head`
Expected: three table entries. Launch `build-sc/pcsx2-qt/PCSX2.app`, open Settings > Hotkeys, confirm the Graphics section lists "Toggle Shader Chain", "Next Shader Preset" and "Previous Shader Preset", then quit.

- [ ] **Step 6: Commit**

```bash
git add pcsx2/VMManager.cpp pcsx2/GS/GS.cpp pcsx2/ImGui/ImGuiOverlays.cpp
git commit -m "GS: Shader chain hotkeys, override push on settings load, overlay entry

- VMManager::ApplySettings() pushes the active preset's parameter overrides into the store
- ToggleShaderChain, NextShaderPreset and PreviousShaderPreset hotkeys; the cycle hotkeys step
  through the ShaderChainFavorites list, skip missing files and enable the chain
- Settings overlay shows SC=<preset> while the chain is active

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Parameter editor dialog and the Parameters... button

**Files:**
- Create: `pcsx2-qt/ShaderParametersDialog.ui`
- Create: `pcsx2-qt/ShaderParametersDialog.h`
- Create: `pcsx2-qt/ShaderParametersDialog.cpp`
- Modify: `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui:301-347` (new button row before `shaderChainStatus`, tab stops)
- Modify: `pcsx2-qt/Settings/GraphicsSettingsWidget.h:47-52,65-66` (slots)
- Modify: `pcsx2-qt/Settings/GraphicsSettingsWidget.cpp:9,233-244,753-766,1005-1013` (wiring, help, enable state)
- Modify: `pcsx2-qt/CMakeLists.txt:37-42`, `pcsx2-qt/pcsx2-qt.vcxproj:177-178,281-282,301-302`, `pcsx2-qt/pcsx2-qt.vcxproj.filters:146-147,405-406,789-790`

**Interfaces:**
- Consumes: `ShaderChainParams::{ParameterInfo, ParamList, EnumerateParameters, ParseOverrides, FormatOverrides, SettingsSection}` (Tasks 1-2); `ShaderPresets::ResolvePresetPath`, `ShaderPresets::Params().Set(preset, list)`; `SettingsWindow::isPerGameSettings()`, `getSettingsInterface()`, `saveAndReloadGameSettings()`, `getEffectiveStringValue`, `registerWidgetHelp`; `SettingsInterface::{ContainsValue, GetStringList, SetStringList, DeleteValue}`; `Host::{GetBaseStringListSetting, SetBaseStringListSettingValue, RemoveBaseSettingValue, CommitBaseSettingChanges}`; `Path::GetFileTitle`.
- Produces: `class ShaderParametersDialog final : public QDialog { ShaderParametersDialog(SettingsWindow* settings, QWidget* parent, std::string preset); }`; `Ui::GraphicsPostProcessingSettingsTab` gains `QPushButton* shaderChainParameters` and `QPushButton* shaderChainFavorites` (the latter is wired in Task 5); `GraphicsSettingsWidget` slots `onShaderChainParametersClicked()` and `onShaderChainFavoritesClicked()` (Task 5 fills in the second one; this task leaves the Favorites button disabled with the tooltip below).

Qt dialogs have no unit-test harness in this repo; verification is a clean build plus the manual steps at the end of this task.

- [ ] **Step 1: Create the `.ui` file**

`pcsx2-qt/ShaderParametersDialog.ui`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>ShaderParametersDialog</class>
 <widget class="QDialog" name="ShaderParametersDialog">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>720</width>
    <height>560</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>Shader Parameters</string>
  </property>
  <layout class="QVBoxLayout" name="verticalLayout">
   <item>
    <widget class="QLabel" name="status">
     <property name="text">
      <string/>
     </property>
     <property name="wordWrap">
      <bool>true</bool>
     </property>
    </widget>
   </item>
   <item>
    <widget class="QScrollArea" name="scroll">
     <property name="widgetResizable">
      <bool>true</bool>
     </property>
     <property name="frameShape">
      <enum>QFrame::Shape::StyledPanel</enum>
     </property>
     <widget class="QWidget" name="scrollContents">
      <layout class="QGridLayout" name="grid">
       <property name="leftMargin">
        <number>8</number>
       </property>
       <property name="topMargin">
        <number>8</number>
       </property>
       <property name="rightMargin">
        <number>8</number>
       </property>
       <property name="bottomMargin">
        <number>8</number>
       </property>
       <property name="horizontalSpacing">
        <number>10</number>
       </property>
      </layout>
     </widget>
    </widget>
   </item>
   <item>
    <layout class="QHBoxLayout" name="footer">
     <item>
      <widget class="QPushButton" name="resetAll">
       <property name="text">
        <string>Reset All</string>
       </property>
      </widget>
     </item>
     <item>
      <spacer name="footerSpacer">
       <property name="orientation">
        <enum>Qt::Orientation::Horizontal</enum>
       </property>
       <property name="sizeHint" stdset="0">
        <size>
         <width>0</width>
         <height>0</height>
        </size>
       </property>
      </spacer>
     </item>
     <item>
      <widget class="QPushButton" name="close">
       <property name="text">
        <string>Close</string>
       </property>
       <property name="default">
        <bool>true</bool>
       </property>
      </widget>
     </item>
    </layout>
   </item>
  </layout>
 </widget>
 <resources/>
 <connections/>
</ui>
```

- [ ] **Step 2: Create the header**

`pcsx2-qt/ShaderParametersDialog.h`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_ShaderParametersDialog.h"

#include "pcsx2/GS/ShaderChain/ShaderChainParams.h"

#include <QtWidgets/QDialog>

#include <string>
#include <vector>

class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QTimer;
class SettingsWindow;

/// Editor for a preset's #pragma parameters. Every change is pushed to the running shader chain
/// immediately and written to the INI (global or per-game layer) after a short debounce.
class ShaderParametersDialog final : public QDialog
{
	Q_OBJECT

public:
	ShaderParametersDialog(SettingsWindow* settings, QWidget* parent, std::string preset);
	~ShaderParametersDialog() override;

protected:
	void done(int r) override;

private Q_SLOTS:
	void onResetAllClicked();
	void flushWrite();

private:
	struct Row
	{
		ShaderChainParams::ParameterInfo info;
		QSlider* slider = nullptr; // null when the range is degenerate
		QDoubleSpinBox* spin = nullptr;
		QPushButton* reset = nullptr;
		float slider_increment = 0.0f; // value per slider position
		float value = 0.0f;
	};

	static constexpr int MAX_SLIDER_STEPS = 10000;
	static constexpr int WRITE_DELAY_MS = 250;

	bool loadParameters();
	void buildRows();
	std::vector<std::string> readOverrideEntries() const;
	void applyOverrides(const ShaderChainParams::ParamList& overrides);
	void setRowValue(Row& row, float value);
	void refreshRowWidgets(Row& row);
	bool isDefault(const Row& row) const;
	ShaderChainParams::ParamList collectOverrides() const;
	void pushToStore();
	void scheduleWrite();
	void onValueEdited(Row& row, float value);

	Ui::ShaderParametersDialog m_ui;
	SettingsWindow* m_settings;
	std::string m_preset;
	std::vector<Row> m_rows;
	QTimer* m_write_timer = nullptr;
	bool m_updating = false;
	bool m_write_pending = false;
};
```

- [ ] **Step 3: Create the implementation**

`pcsx2-qt/ShaderParametersDialog.cpp`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderParametersDialog.h"
#include "Settings/SettingsWindow.h"

#include "pcsx2/GS/ShaderChain/ShaderPresets.h"
#include "pcsx2/Host.h"

#include "common/Error.h"
#include "common/Path.h"
#include "common/SettingsInterface.h"

#include <QtCore/QTimer>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>

#include <algorithm>
#include <cmath>

namespace
{
	int DecimalsForStep(float step)
	{
		int decimals = 0;
		double s = step;
		while (decimals < 4 && std::abs(s - std::round(s)) > 1e-4)
		{
			s *= 10.0;
			decimals++;
		}
		return decimals;
	}
} // namespace

ShaderParametersDialog::ShaderParametersDialog(SettingsWindow* settings, QWidget* parent, std::string preset)
	: QDialog(parent)
	, m_settings(settings)
	, m_preset(std::move(preset))
{
	m_ui.setupUi(this);
	setWindowTitle(tr("Shader Parameters - %1").arg(QString::fromStdString(std::string(Path::GetFileTitle(m_preset)))));

	m_write_timer = new QTimer(this);
	m_write_timer->setSingleShot(true);
	m_write_timer->setInterval(WRITE_DELAY_MS);
	connect(m_write_timer, &QTimer::timeout, this, &ShaderParametersDialog::flushWrite);

	connect(m_ui.resetAll, &QPushButton::clicked, this, &ShaderParametersDialog::onResetAllClicked);
	connect(m_ui.close, &QPushButton::clicked, this, &QDialog::accept);

	if (m_settings->isPerGameSettings())
		m_ui.resetAll->setText(tr("Use Global Settings"));

	if (!loadParameters())
		return;

	buildRows();
	applyOverrides(ShaderChainParams::ParseOverrides(readOverrideEntries()));
}

ShaderParametersDialog::~ShaderParametersDialog() = default;

void ShaderParametersDialog::done(int r)
{
	flushWrite();
	QDialog::done(r);
}

bool ShaderParametersDialog::loadParameters()
{
	const std::string path = ShaderPresets::ResolvePresetPath(m_preset);
	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	if (path.empty() || !ShaderChainParams::EnumerateParameters(path, &params, &error))
	{
		m_ui.status->setText(tr("Could not load preset: %1")
								 .arg(path.empty() ? tr("invalid preset path") : QString::fromStdString(error.GetDescription())));
		m_ui.resetAll->setEnabled(false);
		return false;
	}

	if (params.empty())
	{
		m_ui.status->setText(tr("This preset has no adjustable parameters."));
		m_ui.resetAll->setEnabled(false);
		return false;
	}

	m_ui.status->hide();
	m_rows.reserve(params.size());
	for (ShaderChainParams::ParameterInfo& info : params)
	{
		Row row;
		row.value = info.initial;
		row.info = std::move(info);
		m_rows.push_back(std::move(row));
	}
	return true;
}

void ShaderParametersDialog::buildRows()
{
	QGridLayout* const grid = m_ui.grid;
	for (size_t i = 0; i < m_rows.size(); i++)
	{
		Row& row = m_rows[i];
		const ShaderChainParams::ParameterInfo& info = row.info;
		const int grid_row = static_cast<int>(i);

		QLabel* const label = new QLabel(
			QString::fromStdString(info.description.empty() ? info.name : info.description), m_ui.scrollContents);
		label->setToolTip(QString::fromStdString(info.name));
		grid->addWidget(label, grid_row, 0);

		const bool degenerate = (info.step <= 0.0f || info.maximum <= info.minimum);
		if (!degenerate)
		{
			const int steps = static_cast<int>(std::min<double>(
				std::round((info.maximum - info.minimum) / info.step), static_cast<double>(MAX_SLIDER_STEPS)));
			row.slider_increment = (info.maximum - info.minimum) / static_cast<float>(std::max(steps, 1));
			row.slider = new QSlider(Qt::Horizontal, m_ui.scrollContents);
			row.slider->setRange(0, std::max(steps, 1));
			row.slider->setMinimumWidth(180);
			grid->addWidget(row.slider, grid_row, 1);
			connect(row.slider, &QSlider::valueChanged, this, [this, i](int pos) {
				Row& r = m_rows[i];
				onValueEdited(r, r.info.minimum + static_cast<float>(pos) * r.slider_increment);
			});
		}

		row.spin = new QDoubleSpinBox(m_ui.scrollContents);
		if (degenerate)
		{
			row.spin->setRange(-1.0e9, 1.0e9);
			row.spin->setSingleStep(info.step > 0.0f ? info.step : 1.0);
			row.spin->setDecimals(info.step > 0.0f ? DecimalsForStep(info.step) : 3);
		}
		else
		{
			row.spin->setRange(info.minimum, info.maximum);
			row.spin->setSingleStep(info.step);
			row.spin->setDecimals(DecimalsForStep(info.step));
		}
		row.spin->setMinimumWidth(90);
		grid->addWidget(row.spin, grid_row, 2);
		connect(row.spin, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
			onValueEdited(m_rows[i], static_cast<float>(v));
		});

		row.reset = new QPushButton(tr("Reset"), m_ui.scrollContents);
		grid->addWidget(row.reset, grid_row, 3);
		connect(row.reset, &QPushButton::clicked, this, [this, i]() {
			onValueEdited(m_rows[i], m_rows[i].info.initial);
		});

		refreshRowWidgets(row);
	}
	grid->setColumnStretch(1, 1);
	grid->setRowStretch(static_cast<int>(m_rows.size()), 1);
}

std::vector<std::string> ShaderParametersDialog::readOverrideEntries() const
{
	const char* section = ShaderChainParams::SettingsSection();
	if (m_settings->isPerGameSettings())
	{
		SettingsInterface* const sif = m_settings->getSettingsInterface();
		if (sif->ContainsValue(section, m_preset.c_str()))
			return sif->GetStringList(section, m_preset.c_str());
	}
	return Host::GetBaseStringListSetting(section, m_preset.c_str());
}

void ShaderParametersDialog::applyOverrides(const ShaderChainParams::ParamList& overrides)
{
	for (Row& row : m_rows)
	{
		float value = row.info.initial;
		const auto it = std::find_if(overrides.begin(), overrides.end(),
			[&row](const auto& p) { return p.first == row.info.name; });
		if (it != overrides.end())
		{
			value = it->second;
			// Clamp for display only; the INI keeps the stored value until this row is edited.
			if (row.info.maximum > row.info.minimum)
				value = std::clamp(value, row.info.minimum, row.info.maximum);
		}
		setRowValue(row, value);
	}
}

void ShaderParametersDialog::setRowValue(Row& row, float value)
{
	row.value = value;
	refreshRowWidgets(row);
}

void ShaderParametersDialog::refreshRowWidgets(Row& row)
{
	m_updating = true;
	if (row.slider)
		row.slider->setValue(static_cast<int>(std::lround((row.value - row.info.minimum) / row.slider_increment)));
	row.spin->setValue(row.value);
	row.reset->setEnabled(!isDefault(row));
	m_updating = false;
}

bool ShaderParametersDialog::isDefault(const Row& row) const
{
	const float tolerance = (row.info.step > 0.0f ? row.info.step : 1e-6f) * 0.5f;
	return std::abs(row.value - row.info.initial) < tolerance;
}

ShaderChainParams::ParamList ShaderParametersDialog::collectOverrides() const
{
	ShaderChainParams::ParamList list;
	for (const Row& row : m_rows)
	{
		if (!isDefault(row))
			list.emplace_back(row.info.name, row.value);
	}
	return list;
}

void ShaderParametersDialog::onValueEdited(Row& row, float value)
{
	if (m_updating)
		return;
	if (std::abs(value - row.value) < 1e-7f)
		return;

	setRowValue(row, value);
	pushToStore();
	scheduleWrite();
}

void ShaderParametersDialog::pushToStore()
{
	ShaderPresets::Params().Set(m_preset, collectOverrides());
}

void ShaderParametersDialog::scheduleWrite()
{
	m_write_pending = true;
	m_write_timer->start();
}

void ShaderParametersDialog::flushWrite()
{
	if (!m_write_pending)
		return;
	m_write_pending = false;
	m_write_timer->stop();

	const char* section = ShaderChainParams::SettingsSection();
	const std::vector<std::string> entries = ShaderChainParams::FormatOverrides(collectOverrides());
	if (m_settings->isPerGameSettings())
	{
		SettingsInterface* const sif = m_settings->getSettingsInterface();
		if (entries.empty())
			sif->DeleteValue(section, m_preset.c_str());
		else
			sif->SetStringList(section, m_preset.c_str(), entries);
		m_settings->saveAndReloadGameSettings();
		// A per-game list replaces the global one wholesale, so an emptied list falls back to the
		// global values; show what is now effective.
		if (entries.empty())
			applyOverrides(ShaderChainParams::ParseOverrides(readOverrideEntries()));
	}
	else
	{
		if (entries.empty())
			Host::RemoveBaseSettingValue(section, m_preset.c_str());
		else
			Host::SetBaseStringListSettingValue(section, m_preset.c_str(), entries);
		Host::CommitBaseSettingChanges();
	}

	// Re-push after the write so a settings reload that raced the debounce cannot leave stale values live.
	pushToStore();
}

void ShaderParametersDialog::onResetAllClicked()
{
	if (m_settings->isPerGameSettings())
	{
		// "Use Global Settings": drop the per-game key and show the global values.
		m_write_pending = false;
		m_write_timer->stop();
		m_settings->getSettingsInterface()->DeleteValue(ShaderChainParams::SettingsSection(), m_preset.c_str());
		m_settings->saveAndReloadGameSettings();
		applyOverrides(ShaderChainParams::ParseOverrides(readOverrideEntries()));
		pushToStore();
		return;
	}

	for (Row& row : m_rows)
		setRowValue(row, row.info.initial);
	pushToStore();
	scheduleWrite();
}
```

- [ ] **Step 4: Register the dialog in the build files**

`pcsx2-qt/CMakeLists.txt`: after `ShaderPresetPickerDialog.ui` add

```
	ShaderParametersDialog.cpp
	ShaderParametersDialog.h
	ShaderParametersDialog.ui
```

`pcsx2-qt/pcsx2-qt.vcxproj`: add `<ClCompile Include="ShaderParametersDialog.cpp" />` after the `ShaderPresetPickerDialog.cpp` ClCompile, `<QtMoc Include="ShaderParametersDialog.h" />` after the `ShaderPresetPickerDialog.h` QtMoc, `<QtUi Include="ShaderParametersDialog.ui" />` after the `ShaderPresetPickerDialog.ui` QtUi. Same three lines in `pcsx2-qt/pcsx2-qt.vcxproj.filters` next to their `ShaderPresetPickerDialog` counterparts (lines 147, 406, 790).

- [ ] **Step 5: Add the button row to the Post-Processing tab**

In `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui`, change the status label's row from `3` to `4` and insert this item before it (after the `shaderChainActionsLayout` item that ends with `</layout>\n      </item>`):

```xml
      <item row="3" column="1" colspan="3">
       <layout class="QHBoxLayout" name="shaderChainEditorsLayout">
        <item>
         <widget class="QPushButton" name="shaderChainParameters">
          <property name="text">
           <string>Parameters...</string>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QPushButton" name="shaderChainFavorites">
          <property name="text">
           <string>Favorites...</string>
          </property>
         </widget>
        </item>
        <item>
         <spacer name="shaderChainEditorsSpacer">
          <property name="orientation">
           <enum>Qt::Orientation::Horizontal</enum>
          </property>
          <property name="sizeHint" stdset="0">
           <size>
            <width>0</width>
            <height>0</height>
           </size>
          </property>
         </spacer>
        </item>
       </layout>
      </item>
```

Then the status item becomes `<item row="4" column="0" colspan="4">`. Add `<tabstop>shaderChainParameters</tabstop>` and `<tabstop>shaderChainFavorites</tabstop>` after `<tabstop>shaderChainDownload</tabstop>`.

- [ ] **Step 6: Wire the widget**

`pcsx2-qt/Settings/GraphicsSettingsWidget.h`: after `void onShaderChainDownloadClicked();` add

```cpp
	void onShaderChainParametersClicked();
	void onShaderChainFavoritesClicked();
```

`pcsx2-qt/Settings/GraphicsSettingsWidget.cpp`:

1. Add `#include "ShaderParametersDialog.h"` after `#include "ShaderPresetPickerDialog.h"`.
2. After the `connect(m_post.shaderChainDownload, ...)` line add:

```cpp
	connect(m_post.shaderChainParameters, &QPushButton::clicked, this, &GraphicsSettingsWidget::onShaderChainParametersClicked);
	connect(m_post.shaderChainFavorites, &QPushButton::clicked, this, &GraphicsSettingsWidget::onShaderChainFavoritesClicked);
	if (dialog()->isPerGameSettings())
	{
		m_post.shaderChainFavorites->setEnabled(false);
		m_post.shaderChainFavorites->setToolTip(tr("Favourites are shared by all games and can be edited in the global settings."));
	}
```

3. In the help block after the `shaderChainDownload` entry add:

```cpp
		dialog()->registerWidgetHelp(m_post.shaderChainParameters, tr("Parameters"), tr("N/A"),
			tr("Opens an editor for the selected preset's adjustable parameters. Changes apply immediately while a game is running and are "
			   "saved per preset, globally or for this game only."));
		dialog()->registerWidgetHelp(m_post.shaderChainFavorites, tr("Favorites"), tr("N/A"),
			tr("Edits the ordered list of presets that the Next Shader Preset and Previous Shader Preset hotkeys cycle through."));
```

4. In `onShaderChainEnabledChanged()` add after the `shaderChainUseGlobal->setEnabled(...)` statement:

```cpp
	m_post.shaderChainParameters->setEnabled(
		enabled && !dialog()->getEffectiveStringValue("EmuCore/GS", "ShaderChainPreset", "").empty());
```

5. Add the slots after `onShaderChainDownloadClicked()`:

```cpp
void GraphicsSettingsWidget::onShaderChainParametersClicked()
{
	const std::string preset = dialog()->getEffectiveStringValue("EmuCore/GS", "ShaderChainPreset", "");
	if (preset.empty())
		return;

	ShaderParametersDialog dlg(dialog(), this, preset);
	dlg.exec();
}

void GraphicsSettingsWidget::onShaderChainFavoritesClicked()
{
	// Wired to ShaderFavoritesDialog in the next task.
}
```

- [ ] **Step 7: Build**

Run: `cmake --build build-sc 2>&1 | grep -E "error|warning: unused" | head; cmake --build build-sc 2>&1 | tail -2`
Expected: no errors.

- [ ] **Step 8: Manual check on the Mac**

Launch `build-sc/pcsx2-qt/PCSX2.app`, Settings > Graphics > Post-Processing: with the chain enabled and a preset selected (for example `shaders_slang/crt/crt-royale.slangp`), `Parameters...` is enabled and opens a dialog with one row per parameter; sliders and spin boxes track each other; per-row `Reset` enables after a change; closing and reopening shows the edited value; `Reset All` clears them. Confirm the INI (`~/Library/Application Support/PCSX2/inis/PCSX2.ini`) has a `[ShaderChainParams]` section with the preset key while a value is changed and none after `Reset All`. With no preset selected the button is disabled. Quit.

- [ ] **Step 9: Commit**

```bash
git add pcsx2-qt/ShaderParametersDialog.ui pcsx2-qt/ShaderParametersDialog.h pcsx2-qt/ShaderParametersDialog.cpp \
  pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui pcsx2-qt/Settings/GraphicsSettingsWidget.h \
  pcsx2-qt/Settings/GraphicsSettingsWidget.cpp pcsx2-qt/CMakeLists.txt pcsx2-qt/pcsx2-qt.vcxproj pcsx2-qt/pcsx2-qt.vcxproj.filters
git commit -m "Qt: Add shader parameter editor dialog

- ShaderParametersDialog lists a preset's #pragma parameters with slider, spin box and
  per-row Reset; edits apply to the running chain immediately and are written to the
  ShaderChainParams INI section (global or per-game) after a 250 ms debounce
- Parameters... and Favorites... buttons on the Post-Processing tab (Favorites wired next)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Favourites dialog and the Favorites... button

**Files:**
- Create: `pcsx2-qt/ShaderFavoritesDialog.ui`
- Create: `pcsx2-qt/ShaderFavoritesDialog.h`
- Create: `pcsx2-qt/ShaderFavoritesDialog.cpp`
- Modify: `pcsx2-qt/Settings/GraphicsSettingsWidget.cpp` (`onShaderChainFavoritesClicked`, include)
- Modify: `pcsx2-qt/CMakeLists.txt`, `pcsx2-qt/pcsx2-qt.vcxproj`, `pcsx2-qt/pcsx2-qt.vcxproj.filters` (next to the `ShaderParametersDialog` entries added in Task 4)

**Interfaces:**
- Consumes: `ShaderPresetPickerDialog(QWidget* parent, const QString& current_preset)` with `selectedPreset()`; `ShaderPresets::ResolvePresetPath`; `Host::{GetBaseStringListSetting, SetBaseStringListSettingValue, RemoveBaseSettingValue, CommitBaseSettingChanges}`; `FileSystem::FileExists`.
- Produces: `class ShaderFavoritesDialog final : public QDialog { ShaderFavoritesDialog(QWidget* parent, std::string current_preset); }`, editing `EmuCore/GS` / `ShaderChainFavorites`.

- [ ] **Step 1: Create the `.ui` file**

`pcsx2-qt/ShaderFavoritesDialog.ui`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>ShaderFavoritesDialog</class>
 <widget class="QDialog" name="ShaderFavoritesDialog">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>640</width>
    <height>420</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>Shader Preset Favorites</string>
  </property>
  <layout class="QVBoxLayout" name="verticalLayout">
   <item>
    <widget class="QLabel" name="hint">
     <property name="text">
      <string>The Next Shader Preset and Previous Shader Preset hotkeys cycle through this list in order. Presets whose file is missing are skipped.</string>
     </property>
     <property name="wordWrap">
      <bool>true</bool>
     </property>
    </widget>
   </item>
   <item>
    <layout class="QHBoxLayout" name="body">
     <item>
      <widget class="QListWidget" name="list">
       <property name="selectionMode">
        <enum>QAbstractItemView::SelectionMode::SingleSelection</enum>
       </property>
      </widget>
     </item>
     <item>
      <layout class="QVBoxLayout" name="buttons">
       <item>
        <widget class="QPushButton" name="addCurrent">
         <property name="text">
          <string>Add Current</string>
         </property>
        </widget>
       </item>
       <item>
        <widget class="QPushButton" name="add">
         <property name="text">
          <string>Add...</string>
         </property>
        </widget>
       </item>
       <item>
        <widget class="QPushButton" name="remove">
         <property name="text">
          <string>Remove</string>
         </property>
        </widget>
       </item>
       <item>
        <widget class="QPushButton" name="moveUp">
         <property name="text">
          <string>Move Up</string>
         </property>
        </widget>
       </item>
       <item>
        <widget class="QPushButton" name="moveDown">
         <property name="text">
          <string>Move Down</string>
         </property>
        </widget>
       </item>
       <item>
        <spacer name="buttonSpacer">
         <property name="orientation">
          <enum>Qt::Orientation::Vertical</enum>
         </property>
         <property name="sizeHint" stdset="0">
          <size>
           <width>0</width>
           <height>0</height>
          </size>
         </property>
        </spacer>
       </item>
      </layout>
     </item>
    </layout>
   </item>
   <item>
    <widget class="QDialogButtonBox" name="dialogButtons">
     <property name="standardButtons">
      <set>QDialogButtonBox::StandardButton::Close</set>
     </property>
    </widget>
   </item>
  </layout>
 </widget>
 <tabstops>
  <tabstop>list</tabstop>
  <tabstop>addCurrent</tabstop>
  <tabstop>add</tabstop>
  <tabstop>remove</tabstop>
  <tabstop>moveUp</tabstop>
  <tabstop>moveDown</tabstop>
 </tabstops>
 <resources/>
 <connections>
  <connection>
   <sender>dialogButtons</sender>
   <signal>rejected()</signal>
   <receiver>ShaderFavoritesDialog</receiver>
   <slot>reject()</slot>
  </connection>
 </connections>
</ui>
```

- [ ] **Step 2: Create the header**

`pcsx2-qt/ShaderFavoritesDialog.h`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_ShaderFavoritesDialog.h"

#include <QtWidgets/QDialog>

#include <string>
#include <vector>

class QListWidgetItem;

/// Ordered, global list of presets cycled by the Next/Previous Shader Preset hotkeys.
/// Every change is written to EmuCore/GS/ShaderChainFavorites immediately.
class ShaderFavoritesDialog final : public QDialog
{
	Q_OBJECT

public:
	ShaderFavoritesDialog(QWidget* parent, std::string current_preset);
	~ShaderFavoritesDialog() override;

private Q_SLOTS:
	void onAddCurrentClicked();
	void onAddClicked();
	void onRemoveClicked();
	void onMoveUpClicked();
	void onMoveDownClicked();
	void updateButtons();

private:
	static constexpr const char* SECTION = "EmuCore/GS";
	static constexpr const char* KEY = "ShaderChainFavorites";

	void load();
	void save();
	void addPreset(const std::string& preset);
	void moveSelected(int delta);
	bool contains(const std::string& preset) const;
	std::vector<std::string> entries() const;
	static void decorateItem(QListWidgetItem* item);

	Ui::ShaderFavoritesDialog m_ui;
	std::string m_current;
};
```

- [ ] **Step 3: Create the implementation**

`pcsx2-qt/ShaderFavoritesDialog.cpp`:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderFavoritesDialog.h"
#include "ShaderPresetPickerDialog.h"

#include "pcsx2/GS/ShaderChain/ShaderPresets.h"
#include "pcsx2/Host.h"

#include "common/FileSystem.h"

#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QPushButton>

ShaderFavoritesDialog::ShaderFavoritesDialog(QWidget* parent, std::string current_preset)
	: QDialog(parent)
	, m_current(std::move(current_preset))
{
	m_ui.setupUi(this);

	connect(m_ui.addCurrent, &QPushButton::clicked, this, &ShaderFavoritesDialog::onAddCurrentClicked);
	connect(m_ui.add, &QPushButton::clicked, this, &ShaderFavoritesDialog::onAddClicked);
	connect(m_ui.remove, &QPushButton::clicked, this, &ShaderFavoritesDialog::onRemoveClicked);
	connect(m_ui.moveUp, &QPushButton::clicked, this, &ShaderFavoritesDialog::onMoveUpClicked);
	connect(m_ui.moveDown, &QPushButton::clicked, this, &ShaderFavoritesDialog::onMoveDownClicked);
	connect(m_ui.list, &QListWidget::itemSelectionChanged, this, &ShaderFavoritesDialog::updateButtons);

	load();
	updateButtons();
}

ShaderFavoritesDialog::~ShaderFavoritesDialog() = default;

void ShaderFavoritesDialog::load()
{
	m_ui.list->clear();
	for (const std::string& preset : Host::GetBaseStringListSetting(SECTION, KEY))
	{
		QListWidgetItem* const item = new QListWidgetItem(QString::fromStdString(preset));
		decorateItem(item);
		m_ui.list->addItem(item);
	}
}

void ShaderFavoritesDialog::decorateItem(QListWidgetItem* item)
{
	const std::string full = ShaderPresets::ResolvePresetPath(item->text().toStdString());
	const bool exists = !full.empty() && FileSystem::FileExists(full.c_str());
	QFont font = item->font();
	font.setItalic(!exists);
	item->setFont(font);
	item->setToolTip(exists ? QString() : tr("File not found"));
}

std::vector<std::string> ShaderFavoritesDialog::entries() const
{
	std::vector<std::string> result;
	result.reserve(static_cast<size_t>(m_ui.list->count()));
	for (int i = 0; i < m_ui.list->count(); i++)
		result.push_back(m_ui.list->item(i)->text().toStdString());
	return result;
}

bool ShaderFavoritesDialog::contains(const std::string& preset) const
{
	for (int i = 0; i < m_ui.list->count(); i++)
	{
		if (m_ui.list->item(i)->text().toStdString() == preset)
			return true;
	}
	return false;
}

void ShaderFavoritesDialog::save()
{
	const std::vector<std::string> list = entries();
	if (list.empty())
		Host::RemoveBaseSettingValue(SECTION, KEY);
	else
		Host::SetBaseStringListSettingValue(SECTION, KEY, list);
	Host::CommitBaseSettingChanges();
}

void ShaderFavoritesDialog::addPreset(const std::string& preset)
{
	if (preset.empty() || contains(preset))
		return;

	QListWidgetItem* const item = new QListWidgetItem(QString::fromStdString(preset));
	decorateItem(item);
	m_ui.list->addItem(item);
	m_ui.list->setCurrentItem(item);
	save();
	updateButtons();
}

void ShaderFavoritesDialog::onAddCurrentClicked()
{
	addPreset(m_current);
}

void ShaderFavoritesDialog::onAddClicked()
{
	ShaderPresetPickerDialog dlg(this, QString::fromStdString(m_current));
	if (dlg.exec() != QDialog::Accepted || dlg.selectedPreset().isEmpty())
		return;
	addPreset(dlg.selectedPreset().toStdString());
}

void ShaderFavoritesDialog::onRemoveClicked()
{
	const int row = m_ui.list->currentRow();
	if (row < 0)
		return;
	delete m_ui.list->takeItem(row);
	save();
	updateButtons();
}

void ShaderFavoritesDialog::moveSelected(int delta)
{
	const int row = m_ui.list->currentRow();
	const int target = row + delta;
	if (row < 0 || target < 0 || target >= m_ui.list->count())
		return;

	QListWidgetItem* const item = m_ui.list->takeItem(row);
	m_ui.list->insertItem(target, item);
	m_ui.list->setCurrentItem(item);
	save();
	updateButtons();
}

void ShaderFavoritesDialog::onMoveUpClicked()
{
	moveSelected(-1);
}

void ShaderFavoritesDialog::onMoveDownClicked()
{
	moveSelected(1);
}

void ShaderFavoritesDialog::updateButtons()
{
	const int row = m_ui.list->currentRow();
	const int count = m_ui.list->count();
	m_ui.addCurrent->setEnabled(!m_current.empty() && !contains(m_current));
	m_ui.remove->setEnabled(row >= 0);
	m_ui.moveUp->setEnabled(row > 0);
	m_ui.moveDown->setEnabled(row >= 0 && row + 1 < count);
}
```

- [ ] **Step 4: Register the dialog in the build files**

`pcsx2-qt/CMakeLists.txt`: after `ShaderParametersDialog.ui` add

```
	ShaderFavoritesDialog.cpp
	ShaderFavoritesDialog.h
	ShaderFavoritesDialog.ui
```

`pcsx2-qt/pcsx2-qt.vcxproj` and `pcsx2-qt/pcsx2-qt.vcxproj.filters`: add `<ClCompile Include="ShaderFavoritesDialog.cpp" />`, `<QtMoc Include="ShaderFavoritesDialog.h" />` and `<QtUi Include="ShaderFavoritesDialog.ui" />` next to the matching `ShaderParametersDialog` lines.

- [ ] **Step 5: Wire the button**

In `pcsx2-qt/Settings/GraphicsSettingsWidget.cpp` add `#include "ShaderFavoritesDialog.h"` after `#include "ShaderParametersDialog.h"` and replace the placeholder slot body:

```cpp
void GraphicsSettingsWidget::onShaderChainFavoritesClicked()
{
	ShaderFavoritesDialog dlg(this, dialog()->getEffectiveStringValue("EmuCore/GS", "ShaderChainPreset", ""));
	dlg.exec();
}
```

- [ ] **Step 6: Build**

Run: `cmake --build build-sc 2>&1 | tail -3`
Expected: no errors.

- [ ] **Step 7: Manual check on the Mac**

Launch `build-sc/pcsx2-qt/PCSX2.app`, Settings > Graphics > Post-Processing > `Favorites...`: `Add Current` adds the selected preset and then disables; `Add...` opens the tree picker and adds the choice; `Move Up`/`Move Down` reorder; `Remove` deletes. `~/Library/Application Support/PCSX2/inis/PCSX2.ini` contains `ShaderChainFavorites = <path>` lines under `[EmuCore/GS]` in list order, and the key is gone after removing every entry. Rename a favourite's file on disk, reopen the dialog: the row is italic with the "File not found" tooltip. Restore the file. Quit.

- [ ] **Step 8: Commit**

```bash
git add pcsx2-qt/ShaderFavoritesDialog.ui pcsx2-qt/ShaderFavoritesDialog.h pcsx2-qt/ShaderFavoritesDialog.cpp \
  pcsx2-qt/Settings/GraphicsSettingsWidget.cpp pcsx2-qt/CMakeLists.txt pcsx2-qt/pcsx2-qt.vcxproj pcsx2-qt/pcsx2-qt.vcxproj.filters
git commit -m "Qt: Add shader preset favourites dialog

- ShaderFavoritesDialog edits the ordered EmuCore/GS/ShaderChainFavorites list used by the
  Next/Previous Shader Preset hotkeys: add current, add from the tree picker, remove, reorder
- Missing files are shown in italic; the Favorites... button opens it from the Post-Processing tab

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Cross-platform build, tests and acceptance handoff

**Files:**
- Modify: `docs/superpowers/specs/2026-09-13-shader-parameters-design.md` (append section `9. Results` with the build/test matrix and acceptance outcomes)

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: green builds and tests on macOS arm64 and Windows x64, and an acceptance checklist for the user to run with a game.

No code is written in this task; it is verification only.

- [ ] **Step 1: Mac full build and tests**

```bash
cmake --build build-sc 2>&1 | tail -2
cmake --build build-sc --target core_test 2>&1 | tail -1
PCSX2_LIBRASHADER_PATH=$HOME/deps/lib/librashader.dylib build-sc/tests/ctest/core/core_test 2>&1 | tail -4
```

Expected: build clean; `[  PASSED  ]` with zero failures and zero skipped `ShaderChainParams` tests.

- [ ] **Step 2: Transfer the branch to Windows and build**

```bash
LAST=$(git merge-base master feature/shader-parameters)
git bundle create /tmp/sp.bundle $LAST..feature/shader-parameters && scp /tmp/sp.bundle pcsx2-win:E:/work/sp.bundle
ssh pcsx2-win 'git -C E:\work\pcsx2 fetch E:\work\sp.bundle feature/shader-parameters && git -C E:\work\pcsx2 checkout -B feature/shader-parameters FETCH_HEAD'
ssh pcsx2-win 'schtasks /Run /TN pcsx2-build'
```

Poll `ssh pcsx2-win 'powershell -NoProfile -Command "Get-Content E:\work\pcsx2-build.log -Tail 3"'` until it prints `EXIT_CODE=`. Expected: `EXIT_CODE=0`. (`E:\work\run-build.cmd` builds `Release Clang|x64` with MSBuild; see the remote workflow section in the phase 1 plan.)

- [ ] **Step 3: Windows unit tests**

`E:\work\run-tests-inner.cmd` (called by `E:\work\run-tests.cmd`, log `E:\work\pcsx2-tests.log`) currently configures `build-tests`, builds `core_test`, prepends `E:\work\pcsx2\deps\bin` to `PATH` (so `librashader.dll` finds `dxcompiler.dll`) and runs `core_test.exe --gtest_filter=ShaderPack*`. Edit it over SSH so the last two commands become:

```bat
set PCSX2_LIBRASHADER_PATH=E:\work\pcsx2\deps\bin\librashader.dll
build-tests\tests\ctest\core\core_test.exe
echo EXIT_CODE=%ERRORLEVEL%
```

(Use `scp` of a rewritten file from the Mac rather than in-place `sed`; cmd has none.) Then:

```bash
ssh pcsx2-win 'schtasks /Run /TN pcsx2-tests'
```

Poll `ssh pcsx2-win 'powershell -NoProfile -Command "Get-Content E:\work\pcsx2-tests.log -Tail 6"'` until `EXIT_CODE=` appears. Expected: `EXIT_CODE=0`, `[  PASSED  ]` for every test, and the three `ShaderChainParams.Enumerate*` tests reported as passed, not skipped.

- [ ] **Step 4: Record results and commit**

Append to the spec:

```markdown
## 9. Results

| Check | macOS arm64 | Windows x64 |
|---|---|---|
| Build | pass / fail | pass / fail |
| core_test (incl. EnumerateParameters with library) | N pass | N pass |
```

Fill in the actual numbers. Commit:

```bash
git add docs/superpowers/specs/2026-09-13-shader-parameters-design.md
git commit -m "Docs: Record phase 3 build and test results

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

- [ ] **Step 5: Acceptance checklist for the user (needs a game; run on both machines)**

1. Parameters: open `Parameters...` on `crt-royale` (or any preset with several parameters) while a game runs; drag a slider and see the picture change immediately; close and reopen, the value persists; per-row `Reset` and `Reset All` behave; the INI key appears and disappears accordingly.
2. Per-game: from a game's properties, change a value, confirm the per-game INI has the `[ShaderChainParams]` key, `Use Global Settings` removes it and the global values return.
3. Favourites: add current, add via picker, reorder, remove; entries survive a restart.
4. Hotkeys: bind Toggle, Next and Previous; each shows an OSD message; Next with the chain disabled enables it; a favourite whose file was renamed is skipped and a Console warning names it.
5. Overlay: with `Show Settings` enabled in the OSD settings, `SC=<stem>` appears while the chain is on and disappears when toggled off.

Report the outcome of every item with the renderer used (Metal and Vulkan on macOS, D3D12 and D3D11 or Vulkan on Windows).
