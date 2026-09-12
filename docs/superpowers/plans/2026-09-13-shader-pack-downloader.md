# Shader Pack Downloader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user install, update and remove the three supported RetroArch slang shader packs from a dialog inside PCSX2, into the Shaders folder, with the directory layout the packs expect.

**Architecture:** A core module `pcsx2/ShaderPacks.{h,cpp}` owns the static pack registry, GitHub version resolution (rapidjson), download via the existing `HTTPDownloader`, per-pack marker files listing written files, dependency expansion, install and uninstall. Archive extraction with path hardening lives in `pcsx2/ShaderPackArchive.{h,cpp}` on libzip. The Qt dialog `pcsx2-qt/ShaderPackDownloadDialog` runs the core on a `QtAsyncProgressThread` and is opened from the Shader Chain group of the Post-Processing tab.

**Tech Stack:** C++20, libzip (vendored), rapidjson (vendored, header-only), `common/HTTPDownloader` (curl on macOS, WinHTTP on Windows), Qt 6 widgets, gtest.

**Spec:** `docs/superpowers/specs/2026-09-13-shader-pack-downloader-design.md`

## Global Constraints

- Packs and layout exactly as the spec's section 3 table: `shaders_slang` (repo `libretro/slang-shaders`, branch head `master`, strip 1, into `shaders_slang`), `retro-crisis-gdv-ntsc` (repo `RetroCrisis/Retro-Crisis-GDV-NTSC`, latest release, strip 1, into `shaders_slang/retro crisis`, depends on `shaders_slang`), `satpixie-crt` (repo `Conkwer/satpixie-crt-shader`, latest release excluding assets containing `variants`, strip 4, into `shaders_slang`).
- GitHub API endpoints: `https://api.github.com/repos/<repo>/commits/<branch>` (field `sha`, version = first 12 chars, download `https://github.com/<repo>/archive/<sha>.zip`) and `https://api.github.com/repos/<repo>/releases/latest` (fields `tag_name`, `assets[].name`, `assets[].browser_download_url`; first asset whose name ends in `.zip` and does not contain `asset_exclude`).
- Markers: `<Shaders>/.shaderpacks/<id>.json` with keys `id`, `version`, `source_url`, `installed_at` (UTC `YYYY-MM-DDTHH:MM:SSZ`), `files` (array of '/'-separated paths relative to the Shaders folder, extraction order). Missing or malformed marker = not installed.
- Extraction rules: strip N leading components; skip entries that strip to nothing, directory entries, any path with a `__MACOSX` segment, and files named `.DS_Store`; abort the pack on absolute names, backslashes, `..` segments, or a canonical path outside the destination. Deletion of a previous version happens only after the new archive has downloaded and opened successfully.
- All network use goes through `HTTPDownloader::Create(Host::GetHTTPUserAgent())`; no new networking code in `common/`.
- Nothing is written to the INI; markers are the only state.
- New files carry `// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team` / `// SPDX-License-Identifier: GPL-3.0+`; tabs for indentation; logging via `INFO_LOG`/`WARNING_LOG`/`ERROR_LOG` (`common/Console.h`).
- Every commit message ends with the fixed trailer `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` (do not substitute the running model's name).
- Builds: macOS uses the existing `build-sc` tree (`cmake --build build-sc --target <t>`); never touch `build/`. Windows compiles via `ssh pcsx2-win` using the bundle transfer and `schtasks /Run /TN pcsx2-build` wrapper described in `docs/superpowers/plans/2026-09-12-librashader-shader-chain.md` ("Windows Remote Workflow"); the Windows repo is `E:\work\pcsx2`.
- Unit tests belong to `core_test` (`tests/ctest/core/CMakeLists.txt`); run the binary found with `find build-sc -name core_test -type f -perm +111`.

## File Structure

| File | Responsibility |
|---|---|
| `pcsx2/ShaderPacks.h` / `.cpp` | Pack registry, marker read/write/remove, JSON parsing of GitHub responses, `ResolveLatest`, `ExpandDependencies`, `Install`, `Uninstall` |
| `pcsx2/ShaderPackArchive.h` / `.cpp` | `TransformEntryName` (strip/skip/reject rules) and `ExtractZipToDirectory` on libzip |
| `pcsx2-qt/ShaderPackDownloadDialog.h` / `.cpp` / `.ui` | Dialog: pack table with status, worker thread (resolve / install / uninstall), progress |
| `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui`, `GraphicsSettingsWidget.{h,cpp}` | "Download Shader Packs..." button and preset refresh after the dialog closes |
| `pcsx2/CMakeLists.txt`, `pcsx2/pcsx2.vcxproj(.filters)`, `pcsx2-qt/CMakeLists.txt`, `pcsx2-qt/pcsx2-qt.vcxproj(.filters)` | Registration; rapidjson include for the core |
| `tests/ctest/core/shader_packs_tests.cpp`, `shader_pack_archive_tests.cpp`, `tests/ctest/core/CMakeLists.txt` | Unit tests |

All core functions take an explicit `shaders_root` so tests run in a temp directory; thin overloads without the root use `EmuFolders::Shaders`.

---

### Task 1: Registry, marker files, and build wiring

**Files:**
- Create: `pcsx2/ShaderPacks.h`, `pcsx2/ShaderPacks.cpp`
- Create: `tests/ctest/core/shader_packs_tests.cpp`
- Modify: `tests/ctest/core/CMakeLists.txt:1-7`, `pcsx2/CMakeLists.txt:68` (source list, next to `GameList.cpp`) and `:154` (header list, next to `GameList.h`) and `:1149` (`target_link_libraries(PCSX2_FLAGS INTERFACE ...)` gains `rapidjson`), `pcsx2/pcsx2.vcxproj:40` (include dirs) and the `ClCompile`/`ClInclude` groups, `pcsx2/pcsx2.vcxproj.filters` (filter `Misc`)

**Interfaces:**
- Produces (header, used by every later task):
```cpp
namespace ShaderPacks {
	enum class VersionSource { BranchHead, LatestRelease };
	struct PackInfo {
		const char* id; const char* display_name; const char* description; const char* license;
		const char* github_repo; VersionSource version_source; const char* branch; const char* asset_exclude;
		u32 strip_components; const char* install_subdir; const char* depends_on;
	};
	std::span<const PackInfo> GetPacks();
	const PackInfo* FindPack(std::string_view id);

	struct InstalledPack { std::string id, version, source_url, installed_at; std::vector<std::string> files; };
	std::string GetMarkerPath(const std::string& shaders_root, std::string_view id);
	std::optional<InstalledPack> ReadMarker(const std::string& shaders_root, std::string_view id);
	bool WriteMarker(const std::string& shaders_root, const InstalledPack& pack, Error* error);
	bool RemoveMarker(const std::string& shaders_root, std::string_view id);
	std::optional<InstalledPack> GetInstalled(std::string_view id); // EmuFolders::Shaders
	std::string CurrentTimestamp();                                 // UTC YYYY-MM-DDTHH:MM:SSZ
}
```

- [ ] **Step 1: Write the failing tests**

Create `tests/ctest/core/shader_packs_tests.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPacks.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace
{
	class TempRoot
	{
	public:
		TempRoot()
		{
			for (int i = 0; i < 1000 && m_root.empty(); i++)
			{
				std::string candidate = Path::Combine(std::filesystem::temp_directory_path().string(),
					"pcsx2_shader_packs_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempRoot() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }
		void file(const char* rel, const char* contents = "x")
		{
			const std::string full = Path::Combine(m_root, rel);
			FileSystem::CreateDirectoryPath(std::string(Path::GetDirectory(full)).c_str(), true);
			FileSystem::WriteStringToFile(full.c_str(), contents);
		}

	private:
		std::string m_root;
	};
} // namespace

TEST(ShaderPacks, RegistryHasThreePacksWithSpecLayout)
{
	const auto packs = ShaderPacks::GetPacks();
	ASSERT_EQ(packs.size(), 3u);

	const ShaderPacks::PackInfo* slang = ShaderPacks::FindPack("shaders_slang");
	ASSERT_NE(slang, nullptr);
	EXPECT_STREQ(slang->github_repo, "libretro/slang-shaders");
	EXPECT_EQ(slang->version_source, ShaderPacks::VersionSource::BranchHead);
	EXPECT_STREQ(slang->branch, "master");
	EXPECT_EQ(slang->strip_components, 1u);
	EXPECT_STREQ(slang->install_subdir, "shaders_slang");
	EXPECT_EQ(slang->depends_on, nullptr);

	const ShaderPacks::PackInfo* rc = ShaderPacks::FindPack("retro-crisis-gdv-ntsc");
	ASSERT_NE(rc, nullptr);
	EXPECT_STREQ(rc->github_repo, "RetroCrisis/Retro-Crisis-GDV-NTSC");
	EXPECT_EQ(rc->version_source, ShaderPacks::VersionSource::LatestRelease);
	EXPECT_EQ(rc->strip_components, 1u);
	EXPECT_STREQ(rc->install_subdir, "shaders_slang/retro crisis");
	EXPECT_STREQ(rc->depends_on, "shaders_slang");

	const ShaderPacks::PackInfo* sat = ShaderPacks::FindPack("satpixie-crt");
	ASSERT_NE(sat, nullptr);
	EXPECT_STREQ(sat->github_repo, "Conkwer/satpixie-crt-shader");
	EXPECT_STREQ(sat->asset_exclude, "variants");
	EXPECT_EQ(sat->strip_components, 4u);
	EXPECT_STREQ(sat->install_subdir, "shaders_slang");
	EXPECT_EQ(sat->depends_on, nullptr);

	EXPECT_EQ(ShaderPacks::FindPack("nope"), nullptr);
}

TEST(ShaderPacks, MarkerRoundTrip)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "satpixie-crt").has_value());

	ShaderPacks::InstalledPack pack;
	pack.id = "satpixie-crt";
	pack.version = "20260122";
	pack.source_url = "https://example.invalid/a \"quoted\" name.zip";
	pack.installed_at = "2026-09-13T00:00:00Z";
	pack.files = {"shaders_slang/crt/satpixie-crt.slangp", "shaders_slang/crt/shaders/satpixie/blur_horiz.slang"};

	Error error;
	ASSERT_TRUE(ShaderPacks::WriteMarker(t.root(), pack, &error)) << error.GetDescription();
	EXPECT_TRUE(FileSystem::FileExists(ShaderPacks::GetMarkerPath(t.root(), "satpixie-crt").c_str()));

	const std::optional<ShaderPacks::InstalledPack> back = ShaderPacks::ReadMarker(t.root(), "satpixie-crt");
	ASSERT_TRUE(back.has_value());
	EXPECT_EQ(back->id, pack.id);
	EXPECT_EQ(back->version, pack.version);
	EXPECT_EQ(back->source_url, pack.source_url);
	EXPECT_EQ(back->installed_at, pack.installed_at);
	EXPECT_EQ(back->files, pack.files);

	EXPECT_TRUE(ShaderPacks::RemoveMarker(t.root(), "satpixie-crt"));
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "satpixie-crt").has_value());
}

TEST(ShaderPacks, MalformedMarkerReadsAsNotInstalled)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	t.file(".shaderpacks/shaders_slang.json", "{ not json");
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "shaders_slang").has_value());
	t.file(".shaderpacks/shaders_slang.json", "{\"id\":\"shaders_slang\"}"); // missing version/files
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "shaders_slang").has_value());
}

TEST(ShaderPacks, TimestampIsUtcIso8601)
{
	const std::string ts = ShaderPacks::CurrentTimestamp();
	ASSERT_EQ(ts.size(), 20u) << ts;
	EXPECT_EQ(ts[4], '-');
	EXPECT_EQ(ts[10], 'T');
	EXPECT_EQ(ts[19], 'Z');
}
```
Register it in `tests/ctest/core/CMakeLists.txt`:
```cmake
add_pcsx2_test(core_test
	patch_tests.cpp
	shader_presets_tests.cpp
	librashader_loader_tests.cpp
	shader_packs_tests.cpp
	MockMemoryInterface.h
	StubHost.cpp
)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | grep -m1 -i error`
Expected: `ShaderPacks.h` file not found.

- [ ] **Step 3: Implement the registry and markers**

Create `pcsx2/ShaderPacks.h`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Error;
class HTTPDownloader;
class ProgressCallback;

/// Installs, updates and removes the supported RetroArch slang shader packs under EmuFolders::Shaders.
/// Functions taking shaders_root exist so tests can run in a temporary directory.
namespace ShaderPacks
{
	enum class VersionSource
	{
		BranchHead, ///< version = head commit of `branch`, archive = github.com/<repo>/archive/<sha>.zip
		LatestRelease, ///< version = tag of releases/latest, archive = first matching .zip asset
	};

	struct PackInfo
	{
		const char* id; ///< stable, file-name safe
		const char* display_name;
		const char* description;
		const char* license;
		const char* github_repo; ///< "owner/name"
		VersionSource version_source;
		const char* branch; ///< BranchHead only
		const char* asset_exclude; ///< LatestRelease only; assets whose name contains this are skipped (may be null)
		u32 strip_components; ///< leading path components removed from every archive entry
		const char* install_subdir; ///< relative to the Shaders folder, '/' separators
		const char* depends_on; ///< pack id that must be installed first, or null
	};

	std::span<const PackInfo> GetPacks();
	const PackInfo* FindPack(std::string_view id);

	struct InstalledPack
	{
		std::string id;
		std::string version;
		std::string source_url;
		std::string installed_at;
		std::vector<std::string> files; ///< relative to the Shaders folder, '/' separators
	};

	/// <shaders_root>/.shaderpacks/<id>.json
	std::string GetMarkerPath(const std::string& shaders_root, std::string_view id);
	std::optional<InstalledPack> ReadMarker(const std::string& shaders_root, std::string_view id);
	bool WriteMarker(const std::string& shaders_root, const InstalledPack& pack, Error* error);
	bool RemoveMarker(const std::string& shaders_root, std::string_view id);
	std::optional<InstalledPack> GetInstalled(std::string_view id);

	/// UTC timestamp formatted as YYYY-MM-DDTHH:MM:SSZ.
	std::string CurrentTimestamp();
} // namespace ShaderPacks
```

Create `pcsx2/ShaderPacks.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPacks.h"
#include "Config.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "fmt/chrono.h"
#include "fmt/format.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <ctime>

namespace
{
	constexpr ShaderPacks::PackInfo s_packs[] = {
		{"shaders_slang", "libretro slang shaders", "The RetroArch slang shader collection (CRT, NTSC, scalers, and more). Required by the Retro Crisis presets.",
			"Mixed per-shader licences, see each file", "libretro/slang-shaders", ShaderPacks::VersionSource::BranchHead, "master", nullptr, 1,
			"shaders_slang", nullptr},
		{"retro-crisis-gdv-ntsc", "Retro Crisis GDV-NTSC presets", "CRT presets based on Guest Advanced NTSC, organised by display resolution.",
			"GPL-3.0", "RetroCrisis/Retro-Crisis-GDV-NTSC", ShaderPacks::VersionSource::LatestRelease, nullptr, nullptr, 1,
			"shaders_slang/retro crisis", "shaders_slang"},
		{"satpixie-crt", "satpixie CRT shader", "A lightweight CRT shader with ghosting and chroma options.",
			"See repository licence", "Conkwer/satpixie-crt-shader", ShaderPacks::VersionSource::LatestRelease, nullptr, "variants", 4,
			"shaders_slang", nullptr},
	};

	constexpr const char* MARKER_DIR = ".shaderpacks";
} // namespace

std::span<const ShaderPacks::PackInfo> ShaderPacks::GetPacks()
{
	return s_packs;
}

const ShaderPacks::PackInfo* ShaderPacks::FindPack(std::string_view id)
{
	for (const PackInfo& pack : s_packs)
	{
		if (id == pack.id)
			return &pack;
	}
	return nullptr;
}

std::string ShaderPacks::GetMarkerPath(const std::string& shaders_root, std::string_view id)
{
	return Path::Combine(Path::Combine(shaders_root, MARKER_DIR), fmt::format("{}.json", id));
}

std::optional<ShaderPacks::InstalledPack> ShaderPacks::ReadMarker(const std::string& shaders_root, std::string_view id)
{
	const std::string path = GetMarkerPath(shaders_root, id);
	std::optional<std::string> text = FileSystem::ReadFileToString(path.c_str());
	if (!text.has_value())
		return std::nullopt;

	rapidjson::Document doc;
	doc.Parse(text->data(), text->size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		WARNING_LOG("ShaderPacks: marker {} is not valid JSON, treating pack as not installed.", path);
		return std::nullopt;
	}

	const auto get_string = [&doc](const char* key, std::string* out) {
		const auto it = doc.FindMember(key);
		if (it == doc.MemberEnd() || !it->value.IsString())
			return false;
		out->assign(it->value.GetString(), it->value.GetStringLength());
		return true;
	};

	InstalledPack pack;
	const auto files = doc.FindMember("files");
	if (!get_string("id", &pack.id) || !get_string("version", &pack.version) || files == doc.MemberEnd() || !files->value.IsArray())
	{
		WARNING_LOG("ShaderPacks: marker {} is missing required fields, treating pack as not installed.", path);
		return std::nullopt;
	}
	get_string("source_url", &pack.source_url);
	get_string("installed_at", &pack.installed_at);
	for (const rapidjson::Value& v : files->value.GetArray())
	{
		if (v.IsString())
			pack.files.emplace_back(v.GetString(), v.GetStringLength());
	}
	return pack;
}

bool ShaderPacks::WriteMarker(const std::string& shaders_root, const InstalledPack& pack, Error* error)
{
	const std::string dir = Path::Combine(shaders_root, MARKER_DIR);
	if (!FileSystem::CreateDirectoryPath(dir.c_str(), true, error))
		return false;

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("id");
	writer.String(pack.id.c_str(), static_cast<rapidjson::SizeType>(pack.id.size()));
	writer.Key("version");
	writer.String(pack.version.c_str(), static_cast<rapidjson::SizeType>(pack.version.size()));
	writer.Key("source_url");
	writer.String(pack.source_url.c_str(), static_cast<rapidjson::SizeType>(pack.source_url.size()));
	writer.Key("installed_at");
	writer.String(pack.installed_at.c_str(), static_cast<rapidjson::SizeType>(pack.installed_at.size()));
	writer.Key("files");
	writer.StartArray();
	for (const std::string& file : pack.files)
		writer.String(file.c_str(), static_cast<rapidjson::SizeType>(file.size()));
	writer.EndArray();
	writer.EndObject();

	const std::string path = GetMarkerPath(shaders_root, pack.id);
	if (!FileSystem::WriteBinaryFile(path.c_str(), buffer.GetString(), buffer.GetSize()))
	{
		Error::SetStringFmt(error, "Failed to write marker {}", path);
		return false;
	}
	return true;
}

bool ShaderPacks::RemoveMarker(const std::string& shaders_root, std::string_view id)
{
	const std::string path = GetMarkerPath(shaders_root, id);
	return !FileSystem::FileExists(path.c_str()) || FileSystem::DeleteFilePath(path.c_str());
}

std::optional<ShaderPacks::InstalledPack> ShaderPacks::GetInstalled(std::string_view id)
{
	return ReadMarker(EmuFolders::Shaders, id);
}

std::string ShaderPacks::CurrentTimestamp()
{
	const std::time_t now = std::time(nullptr);
	return fmt::format("{:%Y-%m-%dT%H:%M:%SZ}", fmt::gmtime(now));
}
```
`Error::SetStringFmt(Error*, fmt, args...)` and `Error::SetStringView(Error*, std::string_view)` are static helpers in `common/Error.h` (lines 75 and 86) that accept a null `Error*`; `FileSystem::ReadFileToString(const char*)` returns `std::optional<std::string>`.

Registration:
- `pcsx2/CMakeLists.txt`: add `ShaderPacks.cpp` after `GameList.cpp` (line 68) and `ShaderPacks.h` after `GameList.h` (line 154); add `rapidjson` to the `target_link_libraries(PCSX2_FLAGS INTERFACE` list at line 1149 (any position, e.g. after `libzip::zip`).
- `pcsx2/pcsx2.vcxproj`: add `<ClCompile Include="ShaderPacks.cpp" />` next to `GameList.cpp` and `<ClInclude Include="ShaderPacks.h" />` next to `GameList.h`; extend the include-dir line at line 40 with `;$(SolutionDir)3rdparty\rapidjson\include` (same pattern `pcsx2-qt.vcxproj:51` uses).
- `pcsx2/pcsx2.vcxproj.filters`: both entries under `<Filter>Misc</Filter>`, like `GameList.*`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | tail -1 && $(find build-sc -name core_test -type f -perm +111) --gtest_filter='ShaderPacks.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 5: Commit**

```bash
git add pcsx2/ShaderPacks.h pcsx2/ShaderPacks.cpp tests/ctest/core/shader_packs_tests.cpp tests/ctest/core/CMakeLists.txt pcsx2/CMakeLists.txt pcsx2/pcsx2.vcxproj pcsx2/pcsx2.vcxproj.filters
git commit -m "ShaderPacks: Add pack registry and marker files

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: GitHub response parsing and version resolution

**Files:**
- Modify: `pcsx2/ShaderPacks.h`, `pcsx2/ShaderPacks.cpp`
- Modify: `tests/ctest/core/shader_packs_tests.cpp` (append tests)

**Interfaces:**
- Consumes: `PackInfo`, `VersionSource` (Task 1); `HTTPDownloader` (`common/HTTPDownloader.h`: `CreateRequest(url, callback, progress)`, `PollRequests()`, `HasAnyRequests()`, callback `(s32 status, const std::string& content_type, std::vector<u8> data)`, `HTTP_STATUS_OK == 200`); `Host::GetHTTPUserAgent()`.
- Produces:
```cpp
namespace ShaderPacks {
	struct ResolvedVersion { std::string version; std::string download_url; };
	bool ParseCommitJson(std::string_view json, std::string* sha, Error* error);
	bool ParseReleaseJson(std::string_view json, const char* asset_exclude, ResolvedVersion* out, Error* error);
	std::string GetVersionUrl(const PackInfo& pack);      // API endpoint for the pack
	std::optional<ResolvedVersion> ResolveLatest(const PackInfo& pack, HTTPDownloader& http, ProgressCallback* progress, Error* error);
}
```

- [ ] **Step 1: Write the failing tests**

Append to `tests/ctest/core/shader_packs_tests.cpp`:
```cpp
TEST(ShaderPacks, ParseCommitJsonYieldsSha)
{
	std::string sha;
	Error error;
	ASSERT_TRUE(ShaderPacks::ParseCommitJson(R"({"sha":"0123456789abcdef0123456789abcdef01234567","commit":{}})", &sha, &error)) << error.GetDescription();
	EXPECT_EQ(sha, "0123456789abcdef0123456789abcdef01234567");

	EXPECT_FALSE(ShaderPacks::ParseCommitJson("{ nope", &sha, &error));
	EXPECT_FALSE(error.GetDescription().empty());
	EXPECT_FALSE(ShaderPacks::ParseCommitJson(R"({"commit":{}})", &sha, &error));
}

TEST(ShaderPacks, ParseReleaseJsonPicksMatchingZipAsset)
{
	const char* json = R"({
		"tag_name": "20260122",
		"assets": [
			{"name": "satpixie-crt-shader-variants-20260122.zip", "browser_download_url": "https://x/variants.zip"},
			{"name": "README.md", "browser_download_url": "https://x/readme"},
			{"name": "satpixie-crt-shader-20260122.zip", "browser_download_url": "https://x/main.zip"}
		]})";
	ShaderPacks::ResolvedVersion out;
	Error error;
	ASSERT_TRUE(ShaderPacks::ParseReleaseJson(json, "variants", &out, &error)) << error.GetDescription();
	EXPECT_EQ(out.version, "20260122");
	EXPECT_EQ(out.download_url, "https://x/main.zip");

	// Without an exclusion the first .zip wins.
	ASSERT_TRUE(ShaderPacks::ParseReleaseJson(json, nullptr, &out, &error));
	EXPECT_EQ(out.download_url, "https://x/variants.zip");

	// RetroCrisis publishes a doubled extension; it still ends in .zip.
	const char* rc = R"({"tag_name":"20260820","assets":[{"name":"Retro.Crisis.GDV-NTSC.2026.08.20.zip.zip","browser_download_url":"https://x/rc.zip.zip"}]})";
	ASSERT_TRUE(ShaderPacks::ParseReleaseJson(rc, nullptr, &out, &error));
	EXPECT_EQ(out.version, "20260820");
	EXPECT_EQ(out.download_url, "https://x/rc.zip.zip");
}

TEST(ShaderPacks, ParseReleaseJsonErrors)
{
	ShaderPacks::ResolvedVersion out;
	Error error;
	EXPECT_FALSE(ShaderPacks::ParseReleaseJson("not json", nullptr, &out, &error));
	EXPECT_FALSE(ShaderPacks::ParseReleaseJson(R"({"tag_name":"v1","assets":[{"name":"a.tar.gz","browser_download_url":"u"}]})", nullptr, &out, &error));
	EXPECT_NE(error.GetDescription().find("zip"), std::string::npos);
	EXPECT_FALSE(ShaderPacks::ParseReleaseJson(R"({"assets":[{"name":"a.zip","browser_download_url":"u"}]})", nullptr, &out, &error)); // no tag_name
}

TEST(ShaderPacks, VersionUrlsFollowGitHubApi)
{
	EXPECT_EQ(ShaderPacks::GetVersionUrl(*ShaderPacks::FindPack("shaders_slang")), "https://api.github.com/repos/libretro/slang-shaders/commits/master");
	EXPECT_EQ(ShaderPacks::GetVersionUrl(*ShaderPacks::FindPack("satpixie-crt")), "https://api.github.com/repos/Conkwer/satpixie-crt-shader/releases/latest");
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | grep -m1 -i error`
Expected: `ParseCommitJson` is not a member of `ShaderPacks`.

- [ ] **Step 3: Implement parsing and resolution**

Append to the `ShaderPacks` namespace in `pcsx2/ShaderPacks.h`:
```cpp
	struct ResolvedVersion
	{
		std::string version;
		std::string download_url;
	};

	/// Parses a GitHub commits/<ref> response; sha receives the full commit hash.
	bool ParseCommitJson(std::string_view json, std::string* sha, Error* error);

	/// Parses a GitHub releases/latest response; picks the first asset ending in ".zip" whose name does
	/// not contain asset_exclude (if non-null).
	bool ParseReleaseJson(std::string_view json, const char* asset_exclude, ResolvedVersion* out, Error* error);

	/// GitHub API URL that yields the pack's current version.
	std::string GetVersionUrl(const PackInfo& pack);

	/// Performs the API request synchronously on the calling thread, polling http until it completes.
	std::optional<ResolvedVersion> ResolveLatest(const PackInfo& pack, HTTPDownloader& http, ProgressCallback* progress, Error* error);
```

Append to `pcsx2/ShaderPacks.cpp` (add `#include "common/HTTPDownloader.h"` and `#include "common/StringUtil.h"` to the includes):
```cpp
bool ShaderPacks::ParseCommitJson(std::string_view json, std::string* sha, Error* error)
{
	rapidjson::Document doc;
	doc.Parse(json.data(), json.size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		Error::SetStringView(error, "Commit response is not valid JSON.");
		return false;
	}
	const auto it = doc.FindMember("sha");
	if (it == doc.MemberEnd() || !it->value.IsString() || it->value.GetStringLength() == 0)
	{
		Error::SetStringView(error, "Commit response has no sha field.");
		return false;
	}
	sha->assign(it->value.GetString(), it->value.GetStringLength());
	return true;
}

bool ShaderPacks::ParseReleaseJson(std::string_view json, const char* asset_exclude, ResolvedVersion* out, Error* error)
{
	rapidjson::Document doc;
	doc.Parse(json.data(), json.size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		Error::SetStringView(error, "Release response is not valid JSON.");
		return false;
	}

	const auto tag = doc.FindMember("tag_name");
	if (tag == doc.MemberEnd() || !tag->value.IsString())
	{
		Error::SetStringView(error, "Release response has no tag_name field.");
		return false;
	}

	const auto assets = doc.FindMember("assets");
	if (assets != doc.MemberEnd() && assets->value.IsArray())
	{
		for (const rapidjson::Value& asset : assets->value.GetArray())
		{
			const auto name = asset.FindMember("name");
			const auto url = asset.FindMember("browser_download_url");
			if (name == asset.MemberEnd() || !name->value.IsString() || url == asset.MemberEnd() || !url->value.IsString())
				continue;

			const std::string_view name_sv(name->value.GetString(), name->value.GetStringLength());
			if (!name_sv.ends_with(".zip"))
				continue;
			if (asset_exclude && name_sv.find(asset_exclude) != std::string_view::npos)
				continue;

			out->version.assign(tag->value.GetString(), tag->value.GetStringLength());
			out->download_url.assign(url->value.GetString(), url->value.GetStringLength());
			return true;
		}
	}

	Error::SetStringFmt(error, "Release {} has no matching .zip asset.", std::string_view(tag->value.GetString(), tag->value.GetStringLength()));
	return false;
}

std::string ShaderPacks::GetVersionUrl(const PackInfo& pack)
{
	if (pack.version_source == VersionSource::BranchHead)
		return fmt::format("https://api.github.com/repos/{}/commits/{}", pack.github_repo, pack.branch);
	return fmt::format("https://api.github.com/repos/{}/releases/latest", pack.github_repo);
}

std::optional<ShaderPacks::ResolvedVersion> ShaderPacks::ResolveLatest(const PackInfo& pack, HTTPDownloader& http,
	ProgressCallback* progress, Error* error)
{
	const std::string url = GetVersionUrl(pack);
	bool done = false;
	s32 status = 0;
	std::string body;
	http.CreateRequest(url, [&done, &status, &body](s32 status_code, const std::string&, HTTPDownloader::Request::Data data) {
		status = status_code;
		body.assign(reinterpret_cast<const char*>(data.data()), data.size());
		done = true;
	}, progress);
	if (!PumpUntilDone(http, done, progress, error))
		return std::nullopt;

	if (status != HTTPDownloader::HTTP_STATUS_OK)
	{
		if (status == HTTPDownloader::HTTP_STATUS_CANCELLED)
			Error::SetStringView(error, "Cancelled.");
		else if (status == 403) // unauthenticated GitHub API calls are rate limited per IP
			Error::SetStringView(error, "GitHub API rate limit reached; try again later.");
		else
			Error::SetStringFmt(error, "Version check for {} failed (HTTP {}).", pack.display_name, status);
		return std::nullopt;
	}

	ResolvedVersion out;
	if (pack.version_source == VersionSource::BranchHead)
	{
		std::string sha;
		if (!ParseCommitJson(body, &sha, error))
			return std::nullopt;
		out.version = sha.substr(0, 12);
		out.download_url = fmt::format("https://github.com/{}/archive/{}.zip", pack.github_repo, sha);
	}
	else if (!ParseReleaseJson(body, pack.asset_exclude, &out, error))
	{
		return std::nullopt;
	}
	return out;
}
```
`HTTPDownloader::HTTP_STATUS_OK` and `Request::Data` are declared in `common/HTTPDownloader.h`; `Error::SetStringView` / `Error::SetStringFmt` are static helpers in `common/Error.h` that accept a null `Error*`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | tail -1 && $(find build-sc -name core_test -type f -perm +111) --gtest_filter='ShaderPacks.*'`
Expected: `[  PASSED  ] 8 tests.`

- [ ] **Step 5: Commit**

```bash
git add pcsx2/ShaderPacks.h pcsx2/ShaderPacks.cpp tests/ctest/core/shader_packs_tests.cpp
git commit -m "ShaderPacks: Resolve pack versions from the GitHub API

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Archive extraction with path hardening

**Files:**
- Create: `pcsx2/ShaderPackArchive.h`, `pcsx2/ShaderPackArchive.cpp`
- Create: `tests/ctest/core/shader_pack_archive_tests.cpp`
- Modify: `tests/ctest/core/CMakeLists.txt`, `pcsx2/CMakeLists.txt` (sources next to `ShaderPacks.*`), `pcsx2/pcsx2.vcxproj`, `pcsx2/pcsx2.vcxproj.filters` (filter `Misc`)

**Interfaces:**
- Consumes: libzip (`zip_get_num_entries`, `zip_stat_index`, `zip_fopen_index`, `zip_fread`, `zip_fclose`), `common/ZipHelpers.h` (`zip_open_managed`, `zip_open_buffer_managed`), `FileSystem::{CreateDirectoryPath, WriteBinaryFile}`, `Path::{Combine, Canonicalize, GetDirectory}`, `ProgressCallback` (may be null).
- Produces:
```cpp
namespace ShaderPackArchive {
	enum class EntryDisposition { Extract, Skip, Reject };
	EntryDisposition TransformEntryName(std::string_view entry_name, u32 strip_components, std::string* relative_out, Error* error);
	/// Extracts every entry into dest_dir. Returns false on the first rejected entry, read/write failure, or cancellation;
	/// `written` receives the relative paths (to dest_dir, '/' separators) of the files that were written, in order.
	bool ExtractZipToDirectory(zip_t* zip, const std::string& dest_dir, u32 strip_components, ProgressCallback* progress,
		std::vector<std::string>* written, Error* error);
}
```

- [ ] **Step 1: Write the failing tests**

Create `tests/ctest/core/shader_pack_archive_tests.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPackArchive.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ZipHelpers.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class TempRoot
	{
	public:
		TempRoot()
		{
			for (int i = 0; i < 1000 && m_root.empty(); i++)
			{
				std::string candidate = Path::Combine(std::filesystem::temp_directory_path().string(),
					"pcsx2_shader_pack_archive_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempRoot() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }

	private:
		std::string m_root;
	};

	// entries: (name, content); a name ending in '/' is added as a directory entry.
	std::string MakeZip(const std::string& dir, const std::vector<std::pair<std::string, std::string>>& entries)
	{
		const std::string path = Path::Combine(dir, "test.zip");
		int err = 0;
		zip_t* z = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
		if (!z)
			return {};
		for (const auto& [name, content] : entries)
		{
			if (name.ends_with('/'))
			{
				zip_dir_add(z, name.c_str(), ZIP_FL_ENC_UTF_8);
				continue;
			}
			zip_source_t* src = zip_source_buffer(z, content.data(), content.size(), 0);
			zip_file_add(z, name.c_str(), src, ZIP_FL_ENC_UTF_8);
		}
		if (zip_close(z) != 0)
			return {};
		return path;
	}

	std::string ReadAll(const std::string& path)
	{
		return FileSystem::ReadFileToString(path.c_str()).value_or("<missing>");
	}
} // namespace

TEST(ShaderPackArchive, TransformEntryNameRules)
{
	using ShaderPackArchive::EntryDisposition;
	std::string rel;
	Error error;

	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/crt/a.slangp", 1, &rel, &error), EntryDisposition::Extract);
	EXPECT_EQ(rel, "crt/a.slangp");
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("a/b/c/d/crt/x.slang", 4, &rel, &error), EntryDisposition::Extract);
	EXPECT_EQ(rel, "crt/x.slang");
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/", 1, &rel, &error), EntryDisposition::Skip); // directory
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/crt/", 1, &rel, &error), EntryDisposition::Skip);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("a/b/c/d", 4, &rel, &error), EntryDisposition::Skip); // strips to nothing
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/__MACOSX/crt/._a.slangp", 1, &rel, &error), EntryDisposition::Skip);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/crt/.DS_Store", 1, &rel, &error), EntryDisposition::Skip);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/../evil.slang", 1, &rel, &error), EntryDisposition::Reject);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("/abs/evil.slang", 1, &rel, &error), EntryDisposition::Reject);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top\\crt\\a.slangp", 1, &rel, &error), EntryDisposition::Reject);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("C:/evil.slang", 0, &rel, &error), EntryDisposition::Reject);
}

TEST(ShaderPackArchive, ExtractStripsSkipsAndRecordsFiles)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	const std::string zip_path = MakeZip(t.root(), {
		{"slang-shaders-abc/", ""},
		{"slang-shaders-abc/README.md", "readme"},
		{"slang-shaders-abc/crt/", ""},
		{"slang-shaders-abc/crt/crt-geom.slangp", "shader0 = x"},
		{"slang-shaders-abc/crt/shaders/geom.slang", "#version 450"},
		{"__MACOSX/slang-shaders-abc/crt/._crt-geom.slangp", "junk"},
		{"slang-shaders-abc/crt/.DS_Store", "junk"},
	});
	ASSERT_FALSE(zip_path.empty());

	zip_error_t ze = {};
	auto zip = zip_open_managed(zip_path.c_str(), ZIP_RDONLY, &ze);
	ASSERT_TRUE(zip);

	const std::string dest = Path::Combine(t.root(), "out");
	std::vector<std::string> written;
	Error error;
	ASSERT_TRUE(ShaderPackArchive::ExtractZipToDirectory(zip.get(), dest, 1, nullptr, &written, &error)) << error.GetDescription();

	ASSERT_EQ(written.size(), 3u);
	EXPECT_EQ(written[0], "README.md");
	EXPECT_EQ(written[1], "crt/crt-geom.slangp");
	EXPECT_EQ(written[2], "crt/shaders/geom.slang");
	EXPECT_EQ(ReadAll(Path::Combine(dest, "crt/shaders/geom.slang")), "#version 450");
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(dest, "crt/.DS_Store").c_str()));
	EXPECT_FALSE(FileSystem::DirectoryExists(Path::Combine(dest, "__MACOSX").c_str()));
}

TEST(ShaderPackArchive, ExtractAbortsOnTraversalAndKeepsWrittenList)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	const std::string zip_path = MakeZip(t.root(), {
		{"top/good1.slang", "1"},
		{"top/../escape.slang", "evil"},
		{"top/good2.slang", "2"},
	});
	ASSERT_FALSE(zip_path.empty());

	zip_error_t ze = {};
	auto zip = zip_open_managed(zip_path.c_str(), ZIP_RDONLY, &ze);
	ASSERT_TRUE(zip);

	const std::string dest = Path::Combine(t.root(), "out");
	std::vector<std::string> written;
	Error error;
	EXPECT_FALSE(ShaderPackArchive::ExtractZipToDirectory(zip.get(), dest, 1, nullptr, &written, &error));
	EXPECT_NE(error.GetDescription().find("escape.slang"), std::string::npos) << error.GetDescription();
	ASSERT_EQ(written.size(), 1u);
	EXPECT_EQ(written[0], "good1.slang");
	EXPECT_TRUE(FileSystem::FileExists(Path::Combine(dest, "good1.slang").c_str()));
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(dest, "good2.slang").c_str()));
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(t.root(), "escape.slang").c_str()));
}

TEST(ShaderPackArchive, ExtractWithZeroStripKeepsFullPaths)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	const std::string zip_path = MakeZip(t.root(), {{"retro crisis/4K Flat/RC - PS2.slangp", "#reference x"}});
	ASSERT_FALSE(zip_path.empty());
	zip_error_t ze = {};
	auto zip = zip_open_managed(zip_path.c_str(), ZIP_RDONLY, &ze);
	ASSERT_TRUE(zip);

	const std::string dest = Path::Combine(t.root(), "out");
	std::vector<std::string> written;
	Error error;
	ASSERT_TRUE(ShaderPackArchive::ExtractZipToDirectory(zip.get(), dest, 0, nullptr, &written, &error)) << error.GetDescription();
	ASSERT_EQ(written.size(), 1u);
	EXPECT_EQ(written[0], "retro crisis/4K Flat/RC - PS2.slangp");
	EXPECT_EQ(ReadAll(Path::Combine(dest, "retro crisis/4K Flat/RC - PS2.slangp")), "#reference x");
}
```
Add `shader_pack_archive_tests.cpp` to the `add_pcsx2_test(core_test ...)` list in `tests/ctest/core/CMakeLists.txt`. `core_test` already links `PCSX2`, which links `libzip::zip`; if the test target cannot see `zip.h`, add `target_link_libraries(core_test PRIVATE libzip::zip)` below the existing `target_link_libraries(core_test PUBLIC ...)` block.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | grep -m1 -i error`
Expected: `ShaderPackArchive.h` file not found.

- [ ] **Step 3: Implement the archive module**

Create `pcsx2/ShaderPackArchive.h`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>
#include <string_view>
#include <vector>

class Error;
class ProgressCallback;
struct zip;
typedef struct zip zip_t;

/// Extraction of downloaded shader pack archives into the Shaders folder.
namespace ShaderPackArchive
{
	enum class EntryDisposition
	{
		Extract, ///< write the file at *relative_out
		Skip, ///< directory entry, junk (__MACOSX, .DS_Store), or nothing left after stripping
		Reject, ///< unsafe name; the whole extraction must abort
	};

	/// Applies strip_components and the skip/reject rules to a zip entry name.
	EntryDisposition TransformEntryName(std::string_view entry_name, u32 strip_components, std::string* relative_out, Error* error);

	/// Extracts all entries of zip into dest_dir. Stops at the first rejected entry, write failure or
	/// cancellation and returns false; written always lists the files that were written, in order,
	/// relative to dest_dir with '/' separators. progress may be null.
	bool ExtractZipToDirectory(zip_t* zip, const std::string& dest_dir, u32 strip_components, ProgressCallback* progress,
		std::vector<std::string>* written, Error* error);
} // namespace ShaderPackArchive
```

Create `pcsx2/ShaderPackArchive.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPackArchive.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/StringUtil.h"

#include "fmt/format.h"
#include "zip.h"

#include <memory>

ShaderPackArchive::EntryDisposition ShaderPackArchive::TransformEntryName(std::string_view entry_name, u32 strip_components,
	std::string* relative_out, Error* error)
{
	if (entry_name.empty())
		return EntryDisposition::Skip;

	// Reject anything that could escape the destination before looking at the components.
	if (entry_name.front() == '/' || entry_name.find('\\') != std::string_view::npos ||
		(entry_name.size() >= 2 && entry_name[1] == ':'))
	{
		Error::SetStringFmt(error, "Refusing unsafe archive entry '{}'.", entry_name);
		return EntryDisposition::Reject;
	}

	const bool is_directory = entry_name.back() == '/';
	std::vector<std::string_view> parts;
	size_t start = 0;
	while (start <= entry_name.size())
	{
		const size_t end = entry_name.find('/', start);
		const std::string_view part = entry_name.substr(start, (end == std::string_view::npos) ? std::string_view::npos : end - start);
		if (part == "..")
		{
			Error::SetStringFmt(error, "Refusing unsafe archive entry '{}'.", entry_name);
			return EntryDisposition::Reject;
		}
		if (!part.empty())
			parts.push_back(part);
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}

	if (is_directory || parts.size() <= strip_components)
		return EntryDisposition::Skip;

	for (size_t i = strip_components; i < parts.size(); i++)
	{
		if (parts[i] == "__MACOSX")
			return EntryDisposition::Skip;
	}
	if (parts.back() == ".DS_Store")
		return EntryDisposition::Skip;

	relative_out->clear();
	for (size_t i = strip_components; i < parts.size(); i++)
	{
		if (i > strip_components)
			relative_out->push_back('/');
		relative_out->append(parts[i]);
	}
	return EntryDisposition::Extract;
}

bool ShaderPackArchive::ExtractZipToDirectory(zip_t* zip, const std::string& dest_dir, u32 strip_components,
	ProgressCallback* progress, std::vector<std::string>* written, Error* error)
{
	const zip_int64_t num_entries = zip_get_num_entries(zip, 0);
	if (num_entries < 0)
	{
		Error::SetStringView(error, "Archive has no entries.");
		return false;
	}

	if (!FileSystem::CreateDirectoryPath(dest_dir.c_str(), true, error))
		return false;

	// Canonical destination prefix used to double-check every output path.
	std::string dest_prefix = Path::Canonicalize(dest_dir);
	if (!dest_prefix.empty() && dest_prefix.back() != '/' && dest_prefix.back() != '\\')
		dest_prefix.push_back('/');

	if (progress)
		progress->SetProgressRange(static_cast<u32>(num_entries));

	std::vector<u8> buffer;
	std::string relative;
	for (zip_int64_t i = 0; i < num_entries; i++)
	{
		if (progress && progress->IsCancelled())
		{
			Error::SetStringView(error, "Cancelled.");
			return false;
		}

		zip_stat_t st;
		if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &st) != 0 || !(st.valid & ZIP_STAT_NAME))
		{
			Error::SetStringFmt(error, "Failed to read archive entry {}.", i);
			return false;
		}

		switch (TransformEntryName(st.name, strip_components, &relative, error))
		{
			case EntryDisposition::Skip:
				if (progress)
					progress->SetProgressValue(static_cast<u32>(i + 1));
				continue;
			case EntryDisposition::Reject:
				return false;
			case EntryDisposition::Extract:
				break;
		}

		std::string out_path = Path::Combine(dest_dir, relative);
		const std::string canonical = Path::Canonicalize(out_path);
		if (!canonical.starts_with(dest_prefix))
		{
			Error::SetStringFmt(error, "Refusing to write '{}' outside the destination directory.", st.name);
			return false;
		}

		const std::string parent(Path::GetDirectory(out_path));
		if (!parent.empty() && !FileSystem::CreateDirectoryPath(parent.c_str(), true, error))
			return false;

		zip_file_t* zf = zip_fopen_index(zip, static_cast<zip_uint64_t>(i), 0);
		if (!zf)
		{
			Error::SetStringFmt(error, "Failed to open archive entry '{}'.", st.name);
			return false;
		}
		const size_t size = (st.valid & ZIP_STAT_SIZE) ? static_cast<size_t>(st.size) : 0;
		buffer.resize(size);
		size_t total = 0;
		while (total < size)
		{
			const zip_int64_t got = zip_fread(zf, buffer.data() + total, size - total);
			if (got <= 0)
				break;
			total += static_cast<size_t>(got);
		}
		zip_fclose(zf);
		if (total != size)
		{
			Error::SetStringFmt(error, "Failed to read archive entry '{}'.", st.name);
			return false;
		}

		if (!FileSystem::WriteBinaryFile(out_path.c_str(), buffer.data(), buffer.size()))
		{
			Error::SetStringFmt(error, "Failed to write '{}'.", out_path);
			return false;
		}

		written->push_back(relative);
		if (progress)
			progress->SetProgressValue(static_cast<u32>(i + 1));
	}

	return true;
}
```
On Windows `Path::Canonicalize` returns backslash separators; the prefix comparison works because both sides come from `Path::Canonicalize` of paths built with `Path::Combine`.

Register `ShaderPackArchive.cpp` / `.h` in `pcsx2/CMakeLists.txt` (next to `ShaderPacks.*`), `pcsx2/pcsx2.vcxproj` and `pcsx2/pcsx2.vcxproj.filters` (filter `Misc`).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | tail -1 && $(find build-sc -name core_test -type f -perm +111) --gtest_filter='ShaderPackArchive.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 5: Commit**

```bash
git add pcsx2/ShaderPackArchive.h pcsx2/ShaderPackArchive.cpp tests/ctest/core/shader_pack_archive_tests.cpp tests/ctest/core/CMakeLists.txt pcsx2/CMakeLists.txt pcsx2/pcsx2.vcxproj pcsx2/pcsx2.vcxproj.filters
git commit -m "ShaderPacks: Add hardened zip extraction

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Dependency expansion and uninstall

**Files:**
- Modify: `pcsx2/ShaderPacks.h`, `pcsx2/ShaderPacks.cpp`
- Modify: `tests/ctest/core/shader_packs_tests.cpp` (append tests)

**Interfaces:**
- Consumes: `FindPack`, `ReadMarker`, `RemoveMarker` (Task 1); `FileSystem::{FileExists, DeleteFilePath, FindFiles, DirectoryIsEmpty, DeleteDirectory}`.
- Produces:
```cpp
namespace ShaderPacks {
	/// Returns the ids to install in order: each requested pack preceded by its dependency when that
	/// dependency is neither installed (marker present) nor already earlier in the list. Unknown ids are dropped.
	std::vector<std::string> ExpandDependencies(const std::string& shaders_root, std::span<const std::string> ids);
	/// Deletes the files listed in the pack's marker, prunes empty directories under its install dir, removes the marker.
	bool Uninstall(const std::string& shaders_root, std::string_view id, Error* error);
	/// Removes empty directories below (and including) dir, deepest first.
	void PruneEmptyDirectories(const std::string& dir);
}
```

- [ ] **Step 1: Write the failing tests**

Append to `tests/ctest/core/shader_packs_tests.cpp`:
```cpp
namespace
{
	ShaderPacks::InstalledPack MakeInstalled(const char* id, std::vector<std::string> files)
	{
		ShaderPacks::InstalledPack p;
		p.id = id;
		p.version = "v";
		p.installed_at = "2026-09-13T00:00:00Z";
		p.files = std::move(files);
		return p;
	}
} // namespace

TEST(ShaderPacks, ExpandDependenciesOrdersAndDedupes)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	Error error;

	const std::string rc = "retro-crisis-gdv-ntsc", slang = "shaders_slang", sat = "satpixie-crt";

	std::vector<std::string> ids{rc};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{slang, rc}));

	ids = {rc, slang};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{slang, rc}));

	ids = {sat};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{sat}));

	ids = {"bogus", sat};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{sat}));

	ASSERT_TRUE(ShaderPacks::WriteMarker(t.root(), MakeInstalled("shaders_slang", {"shaders_slang/stock.slang"}), &error));
	ids = {rc};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{rc}));
}

TEST(ShaderPacks, UninstallDeletesListedFilesOnly)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	Error error;

	t.file("shaders_slang/crt/satpixie-crt.slangp");
	t.file("shaders_slang/crt/shaders/satpixie/accumulate.slang");
	t.file("shaders_slang/crt/shaders/satpixie/blur_horiz.slang");
	t.file("shaders_slang/crt/crt-geom.slangp"); // belongs to another pack, must survive
	ASSERT_TRUE(ShaderPacks::WriteMarker(t.root(), MakeInstalled("satpixie-crt", {
		"shaders_slang/crt/satpixie-crt.slangp",
		"shaders_slang/crt/shaders/satpixie/accumulate.slang",
		"shaders_slang/crt/shaders/satpixie/blur_horiz.slang",
		"shaders_slang/crt/shaders/satpixie/already-gone.slang", // missing on disk: tolerated
	}), &error));

	ASSERT_TRUE(ShaderPacks::Uninstall(t.root(), "satpixie-crt", &error)) << error.GetDescription();
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(t.root(), "shaders_slang/crt/satpixie-crt.slangp").c_str()));
	EXPECT_FALSE(FileSystem::DirectoryExists(Path::Combine(t.root(), "shaders_slang/crt/shaders/satpixie").c_str())); // pruned
	EXPECT_FALSE(FileSystem::DirectoryExists(Path::Combine(t.root(), "shaders_slang/crt/shaders").c_str())); // pruned
	EXPECT_TRUE(FileSystem::FileExists(Path::Combine(t.root(), "shaders_slang/crt/crt-geom.slangp").c_str()));
	EXPECT_TRUE(FileSystem::DirectoryExists(Path::Combine(t.root(), "shaders_slang/crt").c_str())); // not empty
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "satpixie-crt").has_value());

	EXPECT_FALSE(ShaderPacks::Uninstall(t.root(), "satpixie-crt", &error)); // not installed
	EXPECT_NE(error.GetDescription().find("not installed"), std::string::npos);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | grep -m1 -i error`
Expected: `ExpandDependencies` is not a member of `ShaderPacks`.

- [ ] **Step 3: Implement**

Append to the namespace in `pcsx2/ShaderPacks.h`:
```cpp
	/// Install order for the requested packs: a missing dependency is inserted before its dependent;
	/// duplicates and unknown ids are dropped.
	std::vector<std::string> ExpandDependencies(const std::string& shaders_root, std::span<const std::string> ids);

	/// Deletes the files recorded in the pack's marker, prunes directories left empty under the pack's
	/// install directory, and removes the marker. Fails if the pack is not installed.
	bool Uninstall(const std::string& shaders_root, std::string_view id, Error* error);

	/// Removes empty directories below and including dir, deepest first.
	void PruneEmptyDirectories(const std::string& dir);
```

Append to `pcsx2/ShaderPacks.cpp` (add `#include <algorithm>`):
```cpp
std::vector<std::string> ShaderPacks::ExpandDependencies(const std::string& shaders_root, std::span<const std::string> ids)
{
	std::vector<std::string> result;
	const auto add = [&result](std::string_view id) {
		if (std::find(result.begin(), result.end(), id) == result.end())
			result.emplace_back(id);
	};

	for (const std::string& id : ids)
	{
		const PackInfo* pack = FindPack(id);
		if (!pack)
		{
			WARNING_LOG("ShaderPacks: ignoring unknown pack id '{}'.", id);
			continue;
		}
		if (pack->depends_on && FindPack(pack->depends_on) && !ReadMarker(shaders_root, pack->depends_on).has_value())
			add(pack->depends_on);
		add(id);
	}
	return result;
}

void ShaderPacks::PruneEmptyDirectories(const std::string& dir)
{
	if (!FileSystem::DirectoryExists(dir.c_str()))
		return;

	FileSystem::FindResultsArray dirs;
	FileSystem::FindFiles(dir.c_str(), "*", FILESYSTEM_FIND_RECURSIVE | FILESYSTEM_FIND_FOLDERS | FILESYSTEM_FIND_HIDDEN_FILES, &dirs);

	// Deepest first, so parents become empty after their children are removed.
	std::sort(dirs.begin(), dirs.end(), [](const FILESYSTEM_FIND_DATA& a, const FILESYSTEM_FIND_DATA& b) {
		return a.FileName.size() > b.FileName.size();
	});
	for (const FILESYSTEM_FIND_DATA& fd : dirs)
	{
		if (FileSystem::DirectoryIsEmpty(fd.FileName.c_str()))
			FileSystem::DeleteDirectory(fd.FileName.c_str());
	}
	if (FileSystem::DirectoryIsEmpty(dir.c_str()))
		FileSystem::DeleteDirectory(dir.c_str());
}

bool ShaderPacks::Uninstall(const std::string& shaders_root, std::string_view id, Error* error)
{
	const PackInfo* pack = FindPack(id);
	if (!pack)
	{
		Error::SetStringFmt(error, "Unknown shader pack '{}'.", id);
		return false;
	}

	const std::optional<InstalledPack> installed = ReadMarker(shaders_root, id);
	if (!installed.has_value())
	{
		Error::SetStringFmt(error, "{} is not installed.", pack->display_name);
		return false;
	}

	for (const std::string& rel : installed->files)
	{
		const std::string path = Path::Combine(shaders_root, rel);
		if (FileSystem::FileExists(path.c_str()) && !FileSystem::DeleteFilePath(path.c_str()))
			WARNING_LOG("ShaderPacks: failed to delete '{}'.", path);
	}

	PruneEmptyDirectories(Path::Combine(shaders_root, pack->install_subdir));

	if (!RemoveMarker(shaders_root, id))
	{
		Error::SetStringFmt(error, "Failed to remove the marker for {}.", pack->display_name);
		return false;
	}
	INFO_LOG("ShaderPacks: uninstalled {} ({} files).", pack->display_name, installed->files.size());
	return true;
}
```
`FILESYSTEM_FIND_DATA::FileName` holds the full path when `FILESYSTEM_FIND_RELATIVE_PATHS` is not set (see `common/FileSystem.h:54-65`).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target core_test 2>&1 | tail -1 && $(find build-sc -name core_test -type f -perm +111) --gtest_filter='ShaderPacks.*'`
Expected: `[  PASSED  ] 10 tests.`

- [ ] **Step 5: Commit**

```bash
git add pcsx2/ShaderPacks.h pcsx2/ShaderPacks.cpp tests/ctest/core/shader_packs_tests.cpp
git commit -m "ShaderPacks: Add dependency expansion and uninstall

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Install pipeline (resolve, download, replace, extract, record)

**Files:**
- Modify: `pcsx2/ShaderPacks.h`, `pcsx2/ShaderPacks.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1-4, `ShaderPackArchive::ExtractZipToDirectory` (Task 3), `zip_open_buffer_managed` (`common/ZipHelpers.h`), `HTTPDownloader` (`Create`, `SetTimeout`, `CreateRequest(url, cb, progress)`, `PollRequests`), `Host::GetHTTPUserAgent()` (`pcsx2/Host.h`), `ProgressCallback`.
- Produces:
```cpp
namespace ShaderPacks {
	struct InstallResult { std::string id; bool success = false; bool cancelled = false; std::string message; };
	/// Installs (or updates) the packs in ExpandDependencies order. Returns one result per pack attempted;
	/// a pack whose dependency failed is reported as failed with "requires <name>". Stops early on cancellation.
	std::vector<InstallResult> Install(const std::string& shaders_root, std::span<const std::string> ids, ProgressCallback* progress);
	std::vector<InstallResult> Install(std::span<const std::string> ids, ProgressCallback* progress); // EmuFolders::Shaders
	bool Uninstall(std::string_view id, Error* error);                                                // EmuFolders::Shaders
}
```
No unit test exercises the network; the pieces it composes are tested in Tasks 1-4 and the whole is verified through the dialog in Task 7. The gate is a clean core build.

- [ ] **Step 1: Declare**

Append to the namespace in `pcsx2/ShaderPacks.h`:
```cpp
	struct InstallResult
	{
		std::string id;
		bool success = false;
		bool cancelled = false;
		std::string message; ///< error text, or empty on success
	};

	/// Installs or updates the packs (dependencies expanded and ordered) under shaders_root, reporting
	/// through progress. Deletes a previously installed version only after the new archive has been
	/// downloaded and validated.
	std::vector<InstallResult> Install(const std::string& shaders_root, std::span<const std::string> ids, ProgressCallback* progress);
	std::vector<InstallResult> Install(std::span<const std::string> ids, ProgressCallback* progress);
	bool Uninstall(std::string_view id, Error* error);
```

- [ ] **Step 2: Implement**

Append to `pcsx2/ShaderPacks.cpp` (add `#include "ShaderPackArchive.h"`, `#include "Host.h"`, `#include "common/ProgressCallback.h"`, `#include "common/Threading.h"`, `#include "common/ZipHelpers.h"`, `#include <thread>`, `#include <chrono>`):
```cpp
namespace
{
	constexpr float DOWNLOAD_TIMEOUT_SECONDS = 600.0f; // HTTPDownloader's timeout is total elapsed time

	bool DownloadToMemory(HTTPDownloader& http, const std::string& url, ProgressCallback* progress, std::vector<u8>* out, Error* error)
	{
		bool done = false;
		s32 status = 0;
		http.CreateRequest(url, [&done, &status, out](s32 status_code, const std::string&, HTTPDownloader::Request::Data data) {
			status = status_code;
			*out = std::move(data);
			done = true;
		}, progress);

		while (!done)
		{
			http.PollRequests();
			if (!done)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		if (status == HTTPDownloader::HTTP_STATUS_CANCELLED)
		{
			Error::SetStringView(error, "Cancelled.");
			return false;
		}
		if (status != HTTPDownloader::HTTP_STATUS_OK)
		{
			Error::SetStringFmt(error, "Download failed (HTTP {}).", status);
			return false;
		}
		if (out->empty())
		{
			Error::SetStringView(error, "Download was empty.");
			return false;
		}
		return true;
	}

	void SetStatus(ProgressCallback* progress, const std::string& text)
	{
		if (progress)
			progress->SetStatusText(text.c_str());
	}

	bool IsCancelled(ProgressCallback* progress)
	{
		return progress && progress->IsCancelled();
	}
} // namespace

std::vector<ShaderPacks::InstallResult> ShaderPacks::Install(const std::string& shaders_root, std::span<const std::string> ids,
	ProgressCallback* progress)
{
	std::vector<InstallResult> results;
	const std::vector<std::string> order = ExpandDependencies(shaders_root, ids);
	if (order.empty())
		return results;

	std::unique_ptr<HTTPDownloader> http = HTTPDownloader::Create(Host::GetHTTPUserAgent());
	if (!http)
	{
		results.push_back({order.front(), false, false, "Failed to create HTTP downloader."});
		return results;
	}
	http->SetTimeout(DOWNLOAD_TIMEOUT_SECONDS);

	if (progress)
		progress->SetCancellable(true);

	std::vector<std::string> failed_ids;
	for (const std::string& id : order)
	{
		InstallResult& result = results.emplace_back();
		result.id = id;
		const PackInfo* pack = FindPack(id);
		if (!pack)
		{
			result.message = fmt::format("Unknown shader pack '{}'.", id);
			continue;
		}

		if (IsCancelled(progress))
		{
			result.cancelled = true;
			result.message = "Cancelled.";
			break;
		}

		if (pack->depends_on && std::find(failed_ids.begin(), failed_ids.end(), pack->depends_on) != failed_ids.end())
		{
			const PackInfo* dep = FindPack(pack->depends_on);
			result.message = fmt::format("Requires {}, which failed to install.", dep ? dep->display_name : pack->depends_on);
			failed_ids.push_back(id);
			continue;
		}

		Error error;

		// 1. Resolve.
		SetStatus(progress, fmt::format("Checking {}...", pack->display_name));
		const std::optional<ResolvedVersion> version = ResolveLatest(*pack, *http, progress, &error);
		if (!version.has_value())
		{
			result.message = error.GetDescription();
			failed_ids.push_back(id);
			continue;
		}

		// 2. Download.
		SetStatus(progress, fmt::format("Downloading {}...", pack->display_name));
		std::vector<u8> archive;
		if (!DownloadToMemory(*http, version->download_url, progress, &archive, &error))
		{
			result.cancelled = (error.GetDescription() == "Cancelled.");
			result.message = error.GetDescription();
			if (result.cancelled)
				break;
			failed_ids.push_back(id);
			continue;
		}
		SetStatus(progress, fmt::format("Downloaded {} ({:.1f} MB).", pack->display_name, static_cast<double>(archive.size()) / 1048576.0));

		// 3. Validate.
		zip_error_t ze = {};
		auto zip = zip_open_buffer_managed(archive.data(), archive.size(), ZIP_RDONLY, 0, &ze);
		if (!zip)
		{
			result.message = fmt::format("Downloaded file for {} is not a valid zip archive.", pack->display_name);
			failed_ids.push_back(id);
			continue;
		}

		// 4. Remove the previous version, now that the replacement is known to be good.
		const std::string install_dir = Path::Combine(shaders_root, pack->install_subdir);
		if (const std::optional<InstalledPack> previous = ReadMarker(shaders_root, id); previous.has_value())
		{
			SetStatus(progress, fmt::format("Removing previous {}...", pack->display_name));
			for (const std::string& rel : previous->files)
			{
				const std::string path = Path::Combine(shaders_root, rel);
				if (FileSystem::FileExists(path.c_str()) && !FileSystem::DeleteFilePath(path.c_str()))
					WARNING_LOG("ShaderPacks: failed to delete '{}'.", path);
			}
			PruneEmptyDirectories(install_dir);
		}

		// 5. Extract.
		SetStatus(progress, fmt::format("Extracting {}...", pack->display_name));
		std::vector<std::string> written;
		const bool extracted = ShaderPackArchive::ExtractZipToDirectory(zip.get(), install_dir, pack->strip_components, progress, &written, &error);

		// 6. Record whatever landed, so a retry or uninstall can clean up.
		InstalledPack marker;
		marker.id = id;
		marker.version = version->version;
		marker.source_url = version->download_url;
		marker.installed_at = CurrentTimestamp();
		marker.files.reserve(written.size());
		for (const std::string& rel : written)
			marker.files.push_back(fmt::format("{}/{}", pack->install_subdir, rel));
		Error marker_error;
		if (!WriteMarker(shaders_root, marker, &marker_error))
			WARNING_LOG("ShaderPacks: {}", marker_error.GetDescription());

		if (!extracted)
		{
			result.cancelled = (error.GetDescription() == "Cancelled.");
			result.message = error.GetDescription();
			if (result.cancelled)
				break;
			failed_ids.push_back(id);
			continue;
		}

		result.success = true;
		INFO_LOG("ShaderPacks: installed {} {} ({} files).", pack->display_name, version->version, written.size());
	}

	return results;
}

std::vector<ShaderPacks::InstallResult> ShaderPacks::Install(std::span<const std::string> ids, ProgressCallback* progress)
{
	return Install(EmuFolders::Shaders, ids, progress);
}

bool ShaderPacks::Uninstall(std::string_view id, Error* error)
{
	return Uninstall(EmuFolders::Shaders, id, error);
}
```
Notes: `HTTPDownloader` only drives `SetProgressRange/Value` when the server sends a size, so GitHub downloads show the "Downloading..." text without a percentage and the size appears once complete (spec section 6 step 2 accepts this). A cancelled download surfaces as `HTTP_STATUS_CANCELLED` through the callback because `LockedPollRequests` checks `progress->IsCancelled()`.

- [ ] **Step 3: Build**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target PCSX2 2>&1 | grep -E "error|warning: " | grep -i shaderpack; cmake --build build-sc --target core_test 2>&1 | tail -1 && $(find build-sc -name core_test -type f -perm +111) --gtest_filter='ShaderPack*'`
Expected: no errors or warnings in `ShaderPacks.cpp`; `[  PASSED  ] 14 tests.` (10 ShaderPacks + 4 ShaderPackArchive).

- [ ] **Step 4: Commit**

```bash
git add pcsx2/ShaderPacks.h pcsx2/ShaderPacks.cpp
git commit -m "ShaderPacks: Add the install pipeline

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Qt dialog and Post-Processing tab button

**Files:**
- Create: `pcsx2-qt/ShaderPackDownloadDialog.h`, `pcsx2-qt/ShaderPackDownloadDialog.cpp`, `pcsx2-qt/ShaderPackDownloadDialog.ui`
- Modify: `pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui` (Shader Chain group, lines 254-315), `pcsx2-qt/Settings/GraphicsSettingsWidget.h:47-49` (slots), `pcsx2-qt/Settings/GraphicsSettingsWidget.cpp:231-239` (connects) and the slot implementations near line 947
- Modify: `pcsx2-qt/CMakeLists.txt:20-22` (after the `CoverDownloadDialog.*` entries), `pcsx2-qt/pcsx2-qt.vcxproj` (`ClCompile` near line 176, `QtMoc` near 278, `QtUi` near 296), `pcsx2-qt/pcsx2-qt.vcxproj.filters` (same three groups near lines 145, 402, 784)

**Interfaces:**
- Consumes: `ShaderPacks::{GetPacks, FindPack, GetInstalled, ResolveLatest, ExpandDependencies, Install, Uninstall, InstalledPack, ResolvedVersion, InstallResult}` (Tasks 1-5), `HTTPDownloader`, `Host::GetHTTPUserAgent`, `QtAsyncProgressThread` (`pcsx2-qt/QtProgressCallback.h`: signals `statusUpdated(QString)`, `progressUpdated(int,int)`, `threadFinished()`; `start()`, `join()`, `requestInterruption()`, virtual `runAsync()`), `GraphicsSettingsWidget::populateShaderChainPresets(bool)`.
- Produces: `class ShaderPackDownloadDialog : public QDialog` with constructor `(QWidget* parent)`; `.ui` widgets `packs`, `note`, `status`, `progress`, `install`, `uninstall`, `close`; Post-Processing widget `shaderChainDownload` and slot `onShaderChainDownloadClicked()`.

No automated UI test; the gate is a clean `pcsx2-qt` build plus a launch/quit check. The click-through happens in Task 7.

- [ ] **Step 1: Create the dialog `.ui`**

`pcsx2-qt/ShaderPackDownloadDialog.ui`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>ShaderPackDownloadDialog</class>
 <widget class="QDialog" name="ShaderPackDownloadDialog">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>760</width>
    <height>420</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>Download Shader Packs</string>
  </property>
  <layout class="QVBoxLayout" name="verticalLayout">
   <item>
    <widget class="QLabel" name="intro">
     <property name="text">
      <string>PCSX2 can download RetroArch slang shader packs into the Shaders folder. Select the packs to install or update, then click Install.</string>
     </property>
     <property name="wordWrap">
      <bool>true</bool>
     </property>
    </widget>
   </item>
   <item>
    <widget class="QTableWidget" name="packs">
     <property name="selectionMode">
      <enum>QAbstractItemView::SelectionMode::NoSelection</enum>
     </property>
     <property name="editTriggers">
      <set>QAbstractItemView::EditTrigger::NoEditTriggers</set>
     </property>
     <attribute name="horizontalHeaderStretchLastSection">
      <bool>true</bool>
     </attribute>
     <attribute name="verticalHeaderVisible">
      <bool>false</bool>
     </attribute>
     <column>
      <property name="text">
       <string>Pack</string>
      </property>
     </column>
     <column>
      <property name="text">
       <string>Description</string>
      </property>
     </column>
     <column>
      <property name="text">
       <string>Licence</string>
      </property>
     </column>
     <column>
      <property name="text">
       <string>Status</string>
      </property>
     </column>
    </widget>
   </item>
   <item>
    <widget class="QLabel" name="note">
     <property name="text">
      <string>Shader packs are third-party content distributed under their own licences. The Retro Crisis presets require the libretro slang shaders, which are selected automatically when needed.</string>
     </property>
     <property name="wordWrap">
      <bool>true</bool>
     </property>
    </widget>
   </item>
   <item>
    <widget class="QLabel" name="status">
     <property name="text">
      <string>Checking installed packs...</string>
     </property>
    </widget>
   </item>
   <item>
    <layout class="QHBoxLayout" name="buttonLayout">
     <item>
      <widget class="QProgressBar" name="progress"/>
     </item>
     <item>
      <widget class="QPushButton" name="install">
       <property name="text">
        <string>Install</string>
       </property>
       <property name="default">
        <bool>true</bool>
       </property>
      </widget>
     </item>
     <item>
      <widget class="QPushButton" name="uninstall">
       <property name="text">
        <string>Uninstall</string>
       </property>
      </widget>
     </item>
     <item>
      <widget class="QPushButton" name="close">
       <property name="text">
        <string>Close</string>
       </property>
      </widget>
     </item>
    </layout>
   </item>
  </layout>
 </widget>
 <tabstops>
  <tabstop>packs</tabstop>
  <tabstop>install</tabstop>
  <tabstop>uninstall</tabstop>
  <tabstop>close</tabstop>
 </tabstops>
 <resources/>
 <connections/>
</ui>
```

- [ ] **Step 2: Create the dialog header**

`pcsx2-qt/ShaderPackDownloadDialog.h`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "QtProgressCallback.h"
#include "ui_ShaderPackDownloadDialog.h"

#include "pcsx2/ShaderPacks.h"

#include <QtWidgets/QDialog>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Installs, updates and removes the supported shader packs. Network and file work run on a worker thread.
class ShaderPackDownloadDialog final : public QDialog
{
	Q_OBJECT

public:
	explicit ShaderPackDownloadDialog(QWidget* parent = nullptr);
	~ShaderPackDownloadDialog();

protected:
	void closeEvent(QCloseEvent* ev) override;

private Q_SLOTS:
	void onWorkerStatus(const QString& text);
	void onWorkerProgress(int value, int range);
	void onWorkerFinished();
	void onInstallClicked();
	void onUninstallClicked();
	void onCloseClicked();

private:
	enum class Mode
	{
		Resolve,
		Install,
		Uninstall,
	};

	struct ResolveOutcome
	{
		std::optional<ShaderPacks::ResolvedVersion> version;
		std::string error;
	};

	class Worker final : public QtAsyncProgressThread
	{
	public:
		Worker(QWidget* parent, Mode mode, std::vector<std::string> ids);
		~Worker() override;

		Mode mode() const { return m_mode; }
		const std::map<std::string, ResolveOutcome>& resolved() const { return m_resolved; }
		const std::vector<ShaderPacks::InstallResult>& results() const { return m_results; }

	protected:
		void runAsync() override;

	private:
		Mode m_mode;
		std::vector<std::string> m_ids;
		std::map<std::string, ResolveOutcome> m_resolved;
		std::vector<ShaderPacks::InstallResult> m_results;
	};

	void populateTable();
	void refreshStatuses();
	std::vector<std::string> checkedIds(bool installed_only) const;
	void startWorker(Mode mode, std::vector<std::string> ids);
	void cancelWorker();
	void updateEnabled();

	Ui::ShaderPackDownloadDialog m_ui;
	std::unique_ptr<Worker> m_worker;
	std::map<std::string, ResolveOutcome> m_resolved;
};
```

- [ ] **Step 3: Create the dialog implementation**

`pcsx2-qt/ShaderPackDownloadDialog.cpp`:
```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPackDownloadDialog.h"

#include "pcsx2/Host.h"

#include "common/Assertions.h"
#include "common/Error.h"
#include "common/HTTPDownloader.h"

#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTableWidgetItem>

namespace
{
	enum Column
	{
		COLUMN_PACK = 0,
		COLUMN_DESCRIPTION,
		COLUMN_LICENSE,
		COLUMN_STATUS,
	};
} // namespace

ShaderPackDownloadDialog::ShaderPackDownloadDialog(QWidget* parent /*= nullptr*/)
	: QDialog(parent)
{
	m_ui.setupUi(this);
	m_ui.packs->horizontalHeader()->setSectionResizeMode(COLUMN_DESCRIPTION, QHeaderView::Stretch);

	connect(m_ui.install, &QPushButton::clicked, this, &ShaderPackDownloadDialog::onInstallClicked);
	connect(m_ui.uninstall, &QPushButton::clicked, this, &ShaderPackDownloadDialog::onUninstallClicked);
	connect(m_ui.close, &QPushButton::clicked, this, &ShaderPackDownloadDialog::onCloseClicked);

	populateTable();
	refreshStatuses();

	// Look up the latest versions in the background; statuses update when it finishes.
	std::vector<std::string> all_ids;
	for (const ShaderPacks::PackInfo& pack : ShaderPacks::GetPacks())
		all_ids.emplace_back(pack.id);
	startWorker(Mode::Resolve, std::move(all_ids));
}

ShaderPackDownloadDialog::~ShaderPackDownloadDialog()
{
	pxAssert(!m_worker);
}

void ShaderPackDownloadDialog::closeEvent(QCloseEvent* ev)
{
	cancelWorker();
}

void ShaderPackDownloadDialog::populateTable()
{
	const auto packs = ShaderPacks::GetPacks();
	m_ui.packs->setRowCount(static_cast<int>(packs.size()));
	int row = 0;
	for (const ShaderPacks::PackInfo& pack : packs)
	{
		QTableWidgetItem* name = new QTableWidgetItem(QString::fromUtf8(pack.display_name));
		name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		name->setCheckState(Qt::Unchecked);
		name->setData(Qt::UserRole, QString::fromUtf8(pack.id));
		m_ui.packs->setItem(row, COLUMN_PACK, name);
		m_ui.packs->setItem(row, COLUMN_DESCRIPTION, new QTableWidgetItem(QString::fromUtf8(pack.description)));
		m_ui.packs->setItem(row, COLUMN_LICENSE, new QTableWidgetItem(QString::fromUtf8(pack.license)));
		m_ui.packs->setItem(row, COLUMN_STATUS, new QTableWidgetItem(tr("Checking...")));
		row++;
	}
	m_ui.packs->resizeColumnsToContents();
}

void ShaderPackDownloadDialog::refreshStatuses()
{
	for (int row = 0; row < m_ui.packs->rowCount(); row++)
	{
		QTableWidgetItem* name = m_ui.packs->item(row, COLUMN_PACK);
		const std::string id = name->data(Qt::UserRole).toString().toStdString();
		const std::optional<ShaderPacks::InstalledPack> installed = ShaderPacks::GetInstalled(id);
		const auto resolved = m_resolved.find(id);

		QString text;
		bool check = false;
		if (resolved == m_resolved.end())
		{
			text = installed ? tr("Installed %1 (checking for updates...)").arg(QString::fromStdString(installed->version)) : tr("Not installed (checking...)");
		}
		else if (!resolved->second.version.has_value())
		{
			text = installed ? tr("Installed %1 (could not check for updates)").arg(QString::fromStdString(installed->version)) :
			                   tr("Not installed (could not check: %1)").arg(QString::fromStdString(resolved->second.error));
			check = !installed;
		}
		else if (!installed)
		{
			text = tr("Not installed (latest %1)").arg(QString::fromStdString(resolved->second.version->version));
			check = true;
		}
		else if (installed->version != resolved->second.version->version)
		{
			text = tr("Update available: %1 → %2").arg(QString::fromStdString(installed->version), QString::fromStdString(resolved->second.version->version));
			check = true;
		}
		else
		{
			text = tr("Installed %1 (up to date)").arg(QString::fromStdString(installed->version));
		}

		m_ui.packs->item(row, COLUMN_STATUS)->setText(text);
		if (resolved != m_resolved.end())
			name->setCheckState(check ? Qt::Checked : Qt::Unchecked);
	}
	m_ui.packs->resizeColumnToContents(COLUMN_STATUS);
}

std::vector<std::string> ShaderPackDownloadDialog::checkedIds(bool installed_only) const
{
	std::vector<std::string> ids;
	for (int row = 0; row < m_ui.packs->rowCount(); row++)
	{
		const QTableWidgetItem* name = m_ui.packs->item(row, COLUMN_PACK);
		if (name->checkState() != Qt::Checked)
			continue;
		const std::string id = name->data(Qt::UserRole).toString().toStdString();
		if (installed_only && !ShaderPacks::GetInstalled(id).has_value())
			continue;
		ids.push_back(id);
	}
	return ids;
}

void ShaderPackDownloadDialog::onWorkerStatus(const QString& text)
{
	m_ui.status->setText(text);
}

void ShaderPackDownloadDialog::onWorkerProgress(int value, int range)
{
	if (range != m_ui.progress->maximum())
		m_ui.progress->setMaximum(range);
	m_ui.progress->setValue(value);
}

void ShaderPackDownloadDialog::onWorkerFinished()
{
	if (!m_worker)
		return;

	m_worker->join();
	const Mode mode = m_worker->mode();

	if (mode == Mode::Resolve)
	{
		m_resolved = m_worker->resolved();
		m_ui.status->setText(tr("Select the packs to install or update."));
	}
	else
	{
		QStringList failed;
		bool cancelled = false;
		for (const ShaderPacks::InstallResult& result : m_worker->results())
		{
			cancelled |= result.cancelled;
			if (!result.success && !result.cancelled)
			{
				const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(result.id);
				failed.append(QStringLiteral("%1: %2").arg(pack ? QString::fromUtf8(pack->display_name) : QString::fromStdString(result.id), QString::fromStdString(result.message)));
			}
		}
		if (cancelled)
			m_ui.status->setText(tr("Cancelled."));
		else if (!failed.isEmpty())
			m_ui.status->setText(tr("Completed with errors: %1").arg(failed.join(QStringLiteral("; "))));
		else
			m_ui.status->setText(mode == Mode::Install ? tr("Done.") : tr("Uninstalled."));
		m_ui.progress->setValue(m_ui.progress->maximum());
	}

	m_worker.reset();
	refreshStatuses();
	updateEnabled();
}

void ShaderPackDownloadDialog::onInstallClicked()
{
	if (m_worker)
	{
		cancelWorker();
		return;
	}

	std::vector<std::string> ids = checkedIds(false);
	if (ids.empty())
	{
		m_ui.status->setText(tr("Select at least one pack."));
		return;
	}

	const std::vector<std::string> expanded = ShaderPacks::ExpandDependencies(EmuFolders::Shaders, ids);
	if (expanded.size() != ids.size())
	{
		QStringList names;
		for (const std::string& id : expanded)
		{
			const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(id);
			names.append(pack ? QString::fromUtf8(pack->display_name) : QString::fromStdString(id));
		}
		m_ui.status->setText(tr("Installing: %1").arg(names.join(QStringLiteral(", "))));
	}
	startWorker(Mode::Install, expanded);
}

void ShaderPackDownloadDialog::onUninstallClicked()
{
	if (m_worker)
		return;

	const std::vector<std::string> ids = checkedIds(true);
	if (ids.empty())
	{
		m_ui.status->setText(tr("Select at least one installed pack."));
		return;
	}

	QStringList names;
	for (const std::string& id : ids)
	{
		const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(id);
		names.append(pack ? QString::fromUtf8(pack->display_name) : QString::fromStdString(id));
	}
	if (QMessageBox::question(this, tr("Uninstall Shader Packs"),
			tr("Remove the files installed by the following packs?\n\n%1").arg(names.join(QStringLiteral("\n")))) != QMessageBox::Yes)
		return;

	startWorker(Mode::Uninstall, ids);
}

void ShaderPackDownloadDialog::onCloseClicked()
{
	cancelWorker();
	done(0);
}

void ShaderPackDownloadDialog::startWorker(Mode mode, std::vector<std::string> ids)
{
	m_worker = std::make_unique<Worker>(this, mode, std::move(ids));
	connect(m_worker.get(), &Worker::statusUpdated, this, &ShaderPackDownloadDialog::onWorkerStatus);
	connect(m_worker.get(), &Worker::progressUpdated, this, &ShaderPackDownloadDialog::onWorkerProgress);
	connect(m_worker.get(), &Worker::threadFinished, this, &ShaderPackDownloadDialog::onWorkerFinished);
	m_ui.progress->setValue(0);
	m_worker->start();
	updateEnabled();
}

void ShaderPackDownloadDialog::cancelWorker()
{
	if (!m_worker)
		return;

	m_worker->requestInterruption();
	m_worker->join();
	m_worker.reset();
	updateEnabled();
}

void ShaderPackDownloadDialog::updateEnabled()
{
	const bool running = static_cast<bool>(m_worker);
	const bool installing = running && m_worker->mode() == Mode::Install;
	m_ui.install->setText(installing ? tr("Cancel") : tr("Install"));
	m_ui.install->setEnabled(!running || installing);
	m_ui.uninstall->setEnabled(!running);
	m_ui.close->setEnabled(!running);
	m_ui.packs->setEnabled(!running);
}

ShaderPackDownloadDialog::Worker::Worker(QWidget* parent, Mode mode, std::vector<std::string> ids)
	: QtAsyncProgressThread(parent)
	, m_mode(mode)
	, m_ids(std::move(ids))
{
}

ShaderPackDownloadDialog::Worker::~Worker() = default;

void ShaderPackDownloadDialog::Worker::runAsync()
{
	switch (m_mode)
	{
		case Mode::Resolve:
		{
			std::unique_ptr<HTTPDownloader> http = HTTPDownloader::Create(Host::GetHTTPUserAgent());
			for (const std::string& id : m_ids)
			{
				const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(id);
				if (!pack)
					continue;
				ResolveOutcome& outcome = m_resolved[id];
				Error error;
				if (http)
					outcome.version = ShaderPacks::ResolveLatest(*pack, *http, this, &error);
				else
					error.SetStringView("Failed to create HTTP downloader.");
				if (!outcome.version.has_value())
					outcome.error = error.GetDescription();
				if (IsCancelled())
					break;
			}
			break;
		}

		case Mode::Install:
			m_results = ShaderPacks::Install(m_ids, this);
			break;

		case Mode::Uninstall:
			for (const std::string& id : m_ids)
			{
				ShaderPacks::InstallResult& result = m_results.emplace_back();
				result.id = id;
				Error error;
				result.success = ShaderPacks::Uninstall(id, &error);
				if (!result.success)
					result.message = error.GetDescription();
			}
			break;
	}
}

#include "moc_ShaderPackDownloadDialog.cpp"
```
`EmuFolders::Shaders` needs `#include "pcsx2/Config.h"`; add it to the includes.

- [ ] **Step 4: Add the button to the Post-Processing tab and wire it**

In `GraphicsPostProcessingSettingsTab.ui`, inside `gridLayout_shaderChain`: change the `shaderChainEnabled` item (row 0) and the `shaderChainStatus` item (row 2) from `colspan="4"` to `colspan="5"`, and add after the `shaderChainOpenFolder` item:
```xml
      <item row="1" column="4">
       <widget class="QPushButton" name="shaderChainDownload">
        <property name="text">
         <string>Download Shader Packs...</string>
        </property>
       </widget>
      </item>
```
Append `<tabstop>shaderChainDownload</tabstop>` after the `shaderChainOpenFolder` tab stop.

In `GraphicsSettingsWidget.h`, after `void onShaderChainOpenFolderClicked();` add `void onShaderChainDownloadClicked();`.

In `GraphicsSettingsWidget.cpp`: add `#include "ShaderPackDownloadDialog.h"`; after the `shaderChainOpenFolder` connect (line 236) add
```cpp
	connect(m_post.shaderChainDownload, &QPushButton::clicked, this, &GraphicsSettingsWidget::onShaderChainDownloadClicked);
```
and next to `onShaderChainOpenFolderClicked()` add
```cpp
void GraphicsSettingsWidget::onShaderChainDownloadClicked()
{
	ShaderPackDownloadDialog dlg(this);
	dlg.exec();
	populateShaderChainPresets(dialog()->isPerGameSettings());
}
```
Add help text next to the existing shader chain `registerWidgetHelp` calls:
```cpp
		dialog()->registerWidgetHelp(m_post.shaderChainDownload, tr("Download Shader Packs"), tr("N/A"),
			tr("Downloads the libretro slang shaders, the Retro Crisis GDV-NTSC presets and the satpixie CRT shader into the Shaders folder, "
			   "and keeps them up to date."));
```

- [ ] **Step 5: Register the new files**

- `pcsx2-qt/CMakeLists.txt`: after `CoverDownloadDialog.ui` (line 22) add `ShaderPackDownloadDialog.cpp`, `ShaderPackDownloadDialog.h`, `ShaderPackDownloadDialog.ui` (keep the list's alphabetical block style).
- `pcsx2-qt/pcsx2-qt.vcxproj`: `<ClCompile Include="ShaderPackDownloadDialog.cpp" />` beside the CoverDownloadDialog `ClCompile` (line 176), `<QtMoc Include="ShaderPackDownloadDialog.h" />` beside line 278, `<QtUi Include="ShaderPackDownloadDialog.ui" />` beside line 296.
- `pcsx2-qt/pcsx2-qt.vcxproj.filters`: the same three entries in the same groups as `CoverDownloadDialog.*` (lines 145, 402, 784), with identical `<Filter>` children if those entries have any.

- [ ] **Step 6: Build and launch check**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target pcsx2-qt 2>&1 | grep -E " error|warning: " | grep -i "ShaderPack\|GraphicsSettings"; cmake --build build-sc --target pcsx2-qt 2>&1 | tail -1`
Expected: no errors or warnings in the touched files. Then `open build-sc/pcsx2-qt/PCSX2.app`, wait 10 s, `osascript -e 'quit app "PCSX2"'`, and confirm no new PCSX2 entry in `ls -t ~/Library/Logs/DiagnosticReports | head -3`.

- [ ] **Step 7: Commit**

```bash
git add pcsx2-qt/ShaderPackDownloadDialog.h pcsx2-qt/ShaderPackDownloadDialog.cpp pcsx2-qt/ShaderPackDownloadDialog.ui pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui pcsx2-qt/Settings/GraphicsSettingsWidget.h pcsx2-qt/Settings/GraphicsSettingsWidget.cpp pcsx2-qt/CMakeLists.txt pcsx2-qt/pcsx2-qt.vcxproj pcsx2-qt/pcsx2-qt.vcxproj.filters
git commit -m "Qt: Add the Download Shader Packs dialog

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Windows build, unit tests on both machines, and manual verification

**Files:**
- Modify: `docs/superpowers/specs/2026-09-13-shader-pack-downloader-design.md` (section 9: record results)

**Interfaces:** none (verification only).

- [ ] **Step 1: Windows compile and unit tests**

Transfer and build with MSBuild exactly as the phase 1 plan's "Windows Remote Workflow" describes (bundle `master..feature/shader-pack-downloader`, `git -C E:\work\pcsx2 fetch E:\work\sc.bundle feature/shader-pack-downloader && git -C E:\work\pcsx2 checkout -B feature/shader-pack-downloader FETCH_HEAD`, then `schtasks /Run /TN pcsx2-build` and poll `E:\work\pcsx2-build.log` for `EXIT_CODE=0`).

Then run the unit tests on Windows through a CMake tree, since the path-handling code must be exercised with backslash separators. Upload two wrappers (CRLF, created with `printf '...\r\n'`): `E:\work\run-tests.cmd` containing `set VSCMD_SKIP_SENDTELEMETRY=1` and `call E:\work\run-tests-inner.cmd > E:\work\pcsx2-tests.log 2>&1`; `E:\work\run-tests-inner.cmd` containing:
```
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d E:\work\pcsx2
cmake -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=E:\work\pcsx2\deps -DQT_BUILD=ON -DDISABLE_ADVANCE_SIMD=ON
cmake --build build-tests --target core_test
build-tests\tests\ctest\core\core_test.exe --gtest_filter=ShaderPack*
echo EXIT_CODE=%ERRORLEVEL%
```
Run it with `schtasks /Create /TN pcsx2-tests /TR E:\work\run-tests.cmd /SC ONCE /ST 00:00 /F && schtasks /Run /TN pcsx2-tests`, poll the log until `EXIT_CODE=`, and expect `[  PASSED  ] 14 tests.` and `EXIT_CODE=0`. (The first configure builds much of the core for the test target; allow 10-15 minutes.) If `core_test.exe` is produced under a different directory, locate it with `dir /s /b E:\work\pcsx2\build-tests\core_test.exe`.

- [ ] **Step 2: Mac unit tests and app build**

Run: `cd ~/work/pcsx2 && cmake --build build-sc --target unittests 2>&1 | grep -E "tests passed|Failed"` — expect `100% tests passed`.

- [ ] **Step 3: Manual click-through (user-driven)**

The dialog cannot be driven by the automation available on either machine, so hand this checklist to the user with the app built at `build-sc/pcsx2-qt/PCSX2.app` (Mac) and `E:\work\pcsx2\bin\pcsx2-qtx64-clang.exe` (Windows), and collect the outcomes:

1. Move the existing `shaders_slang` directory aside (`mv ~/Library/Application\ Support/PCSX2/shaders/shaders_slang ~/Desktop/shaders_slang.bak` on the Mac; on Windows rename `C:\Users\Ilya\Documents\PCSX2\shaders\shaders_slang`). Open Settings > Graphics > Post-Processing > Download Shader Packs...: all three rows read "Not installed (latest ...)" and are checked. Click Install; watch the status label move through Checking / Downloading / Extracting for each pack; expect "Done." and three "Installed ... (up to date)" rows. Close the dialog: the preset combobox lists `shaders_slang/crt/crt-geom.slangp`, `shaders_slang/crt/satpixie-crt.slangp` and the `retro crisis/...` presets.
2. Re-open the dialog: rows show Installed and are unchecked.
3. Check only satpixie, click Uninstall, confirm: `shaders_slang/crt/satpixie-crt.slangp` and `crt/shaders/satpixie/` are gone, `crt/crt-geom.slangp` remains, the combobox no longer lists satpixie.
4. Uninstall the libretro pack and Retro Crisis; check only Retro Crisis and click Install: the status label announces both packs; both install; markers `.shaderpacks/shaders_slang.json` and `retro-crisis-gdv-ntsc.json` exist.
5. Start an install and click Cancel during the libretro download: status "Cancelled.", no partially extracted `shaders_slang` tree, no marker.
6. Disable networking, open the dialog: statuses read "could not check"; Install reports the version-check error and leaves files alone.
7. Windows only: with the packs installed via the dialog, enable the chain with `shaders_slang/retro crisis/4K Flat/RC GDV-NTSC - PS2 - Clean.slangp` on D3D12 and start a game; the effect renders and `emulog.txt` shows `ShaderChain(D3D12): loaded`.

- [ ] **Step 4: Record results and commit**

In spec section 9 add a "Results" list with the date, machine, and pass/fail per item above (mark items the user did not run as "not run"). Commit:
```bash
git add docs/superpowers/specs/2026-09-13-shader-pack-downloader-design.md
git commit -m "Docs: Record shader pack downloader verification results

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```
