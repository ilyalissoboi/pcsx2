# Design: Shader pack downloader (phase 2)

Date: 2026-09-13. Base: PCSX2 fork `master` @ `98c46f497` (phase 1 shader chain merged).
Related: `docs/superpowers/specs/2026-09-12-librashader-shader-chain-design.md` (phase 1),
`docs/superpowers/analysis/2026-09-12-librashader-post-processing-analysis.md` (section 5.6).

## 1. Goal and scope

Let the user install, update and remove the three supported RetroArch slang shader packs from
inside PCSX2, into the Shaders folder created in phase 1, with the directory layout those packs
expect, without leaving PCSX2 or knowing where the files go.

In scope:
- Core module `pcsx2/ShaderPacks.{h,cpp}`: pack registry, GitHub version resolution, download,
  zip extraction with path hardening, marker files, dependency ordering, uninstall.
- Qt dialog `pcsx2-qt/ShaderPackDownloadDialog` launched from the Post-Processing tab.
- Unit tests for every non-network part; manual verification on both machines.

Out of scope: arbitrary user-added sources or a remote manifest; parameter editing; hotkeys;
downloading anything other than the three packs below.

Decisions taken during brainstorming:
- Hardcoded pack table with latest-version resolution through the public GitHub API.
- The libretro pack is fetched from the GitHub mirror `libretro/slang-shaders` (versioned by the
  branch head commit), not from the buildbot zip, which exposes no version to our downloader.
- Marker file per pack listing the written files; updates delete exactly those files first.
- Modal dialog opened from the Shader Chain group; no INI state.
- In-memory download through the existing `HTTPDownloader`; no streaming changes to common code.

## 2. Architecture

```
pcsx2-qt/ShaderPackDownloadDialog            (Qt, UI thread)
   |  QtAsyncProgressThread subclass -> runAsync()
   v
pcsx2/ShaderPacks.{h,cpp}                    (core, any thread, ProgressCallback)
   |- GetPacks()               static table
   |- ResolveLatest()          GitHub API via HTTPDownloader + rapidjson
   |- Install(ids, progress)   expand deps -> per pack: resolve, download, validate, remove old, extract, record
   |- Uninstall(id)            delete listed files, prune dirs, remove marker
   |- ExtractZipToDirectory()  libzip (zip_open_buffer_managed), strip components, hardening
   '- markers                  <Shaders>/.shaderpacks/<id>.json
```

Dependencies already available to the core: `common/HTTPDownloader`, `common/ZipHelpers.h` +
libzip, `common/FileSystem`, `common/Path`, `common/ProgressCallback`, `EmuFolders::Shaders`.
New link: header-only `rapidjson` (CMake target `rapidjson`, MSBuild include
`$(SolutionDir)3rdparty\rapidjson\include`), exactly as `pcsx2-qt` already consumes it.

## 3. Pack registry

```cpp
namespace ShaderPacks {
	enum class VersionSource { BranchHead, LatestRelease };
	struct PackInfo {
		const char* id;               // marker/file-safe id
		const char* display_name;
		const char* description;
		const char* license;
		const char* github_repo;      // "owner/name"
		VersionSource version_source;
		const char* branch;           // BranchHead only, e.g. "master"
		const char* asset_exclude;    // LatestRelease only; asset names containing this are skipped (may be null)
		u32 strip_components;
		const char* install_subdir;   // relative to EmuFolders::Shaders, '/' separators
		const char* depends_on;       // pack id or null
	};
	std::span<const PackInfo> GetPacks();
	const PackInfo* FindPack(std::string_view id);
}
```

| id | display | repo | version | strip | install_subdir | depends_on | licence note |
|---|---|---|---|---|---|---|---|
| `shaders_slang` | libretro slang shaders | `libretro/slang-shaders` | BranchHead `master` | 1 | `shaders_slang` | none | Mixed per-shader licences, see each file |
| `retro-crisis-gdv-ntsc` | Retro Crisis GDV-NTSC presets | `RetroCrisis/Retro-Crisis-GDV-NTSC` | LatestRelease | 1 | `shaders_slang/retro crisis` | `shaders_slang` | GPL-3.0 |
| `satpixie-crt` | satpixie CRT shader | `Conkwer/satpixie-crt-shader` | LatestRelease, exclude `variants` | 4 | `shaders_slang` | none | See repository licence |

Layout rationale: RetroCrisis presets reference `../../../shaders_slang/...`, which resolves only
from `<Shaders>/shaders_slang/retro crisis/<res>/`; satpixie's archive nests
`satpixie-crt-shader/RetroArch/shaders/shaders_slang/crt/...`, so four components are stripped and
it lands in the shared `crt/` tree; the GitHub branch archive wraps everything in
`slang-shaders-<sha>/`, hence one component stripped.

## 4. Version resolution

```cpp
struct ResolvedVersion { std::string version; std::string download_url; };
std::optional<ResolvedVersion> ResolveLatest(const PackInfo& pack, HTTPDownloader& http, ProgressCallback* progress, Error* error);
```
- BranchHead: `GET https://api.github.com/repos/<repo>/commits/<branch>` → `sha`; `version` is
  the first 12 characters; `download_url` is `https://github.com/<repo>/archive/<sha>.zip`.
- LatestRelease: `GET https://api.github.com/repos/<repo>/releases/latest` → `tag_name` as
  `version`; the first element of `assets[]` whose `name` ends with `.zip` and (if `asset_exclude`
  is set) does not contain it; `browser_download_url` as `download_url`. No matching asset is an
  error.
- The request runs synchronously on the calling thread: `CreateRequest`, then poll until the
  callback fires. `progress` may be null; when it is not it is attached to the request, so
  cancelling it aborts the wait ("Cancelled."). The same poll helper is used for the archive
  download and gives up with "Failed to start the request." when `CreateRequest` never queued
  anything, so a failed request cannot hang the wait until the timeout.
- Requests use `Host::GetHTTPUserAgent()` (GitHub requires a User-Agent). The downloader's timeout
  covers total elapsed time, not per-transfer idle time, so `Install` sets it per step: 30 s around
  resolution, 600 s around the download. HTTP 403 (unauthenticated GitHub API calls are rate
  limited per IP) is reported as "GitHub API rate limit reached; try again later."; any other
  non-200 status as "Version check for <name> failed (HTTP <n>).".
- JSON is parsed with rapidjson into plain structs; the parsing functions
  `ParseReleaseJson(std::string_view json, const char* asset_exclude, Error*)` and
  `ParseCommitJson(std::string_view json, Error*)` are pure and unit-tested.
- Resolution runs when the dialog opens (one request per pack) and again inside `Install`.

## 5. On-disk state

Marker path: `<Shaders>/.shaderpacks/<id>.json`. Contents:
```json
{ "id": "satpixie-crt", "version": "20260122",
  "source_url": "https://github.com/.../satpixie-crt-shader-20260122.zip",
  "installed_at": "2026-09-13T00:40:12Z",
  "files": ["shaders_slang/crt/satpixie-crt.slangp", "shaders_slang/crt/shaders/satpixie/accumulate.slang", ...] }
```
`files` are '/'-separated paths relative to the Shaders folder, in extraction order. Markers are
outside the pack directories so `ShaderPresets::Enumerate()` never lists them (`.shaderpacks`
starts with '.' and is already filtered).

```cpp
struct InstalledPack { std::string id, version, source_url, installed_at; std::vector<std::string> files; };
std::optional<InstalledPack> GetInstalled(std::string_view id);   // nullopt if missing or malformed
bool WriteMarker(const InstalledPack&, Error*);
```
Status derivation for the UI: no marker → NotInstalled; marker and resolved version equal →
Installed; differ → UpdateAvailable; marker but resolution failed → Installed with
"could not check for updates".

## 6. Install and uninstall

```cpp
bool Install(std::span<const std::string> ids, ProgressCallback* progress);  // false if any pack failed
bool Uninstall(std::string_view id, Error* error);
std::vector<std::string> ExpandDependencies(std::span<const std::string> ids); // adds missing deps first
bool ExtractZipToDirectory(zip_t* zip, const std::string& dest_dir, u32 strip_components,
	ProgressCallback* progress, std::vector<std::string>* written_relative_to_dest, Error* error);
```

Per pack, each step is a `ProgressCallback` status text:
1. **Resolve** ("Checking <name>..."). Failure: record message, skip pack, continue.
2. **Download** ("Downloading <name>..."): one `CreateRequest` with `progress` attached; the same
   poll loop as resolution; `IsCancelled()` aborts (HTTPDownloader cancels the request). Non-200 or
   empty body: skip pack. The size is only known once the body has arrived, so it is reported
   afterwards as "Downloaded <name> (12.3 MB).".
3. **Validate**: `zip_open_buffer_managed`; failure: skip pack. Nothing on disk has changed yet.
4. **Remove previous** (only if a marker exists): delete each listed file that exists, then prune
   directories under `<Shaders>/<install_subdir>` that became empty. Failures are logged and do not
   stop the install.
5. **Extract** (status "Extracting <name>...", with the entry count n/N on the progress bar) into
   `<Shaders>/<install_subdir>` with `strip_components`. Per entry: split on '/'; drop empty and
   `.` segments, then the first `strip_components` segments; skip if nothing remains, if the entry
   is a directory, if any segment is `__MACOSX`, or if the file name is `.DS_Store`; reject (abort
   this pack) if the original name is absolute, contains `\`, or any segment is `..`, or if
   `Path::Canonicalize(dest + rel)` does not start with `dest`. The entry size comes from the
   archive, so it is checked against `MAX_ENTRY_SIZE` (64 MB) and a running total against
   `MAX_TOTAL_SIZE` (1 GB) before anything is allocated for it; over either cap aborts the pack
   with "Archive entry '<name>' is too large (<n> bytes).". Create parent directories, write with
   `FileSystem::WriteBinaryFile`, append the path (relative to `dest_dir`; `Install` prefixes
   `install_subdir` so marker entries are relative to the Shaders folder) to the written list,
   advance progress.
6. **Record**: write the marker with `version`, `source_url`, timestamp and the written list.
   On a step-5 abort the marker is still written for the files that landed, so Uninstall or a
   retry can clean up, but `version` is left empty so the partial tree is never mistaken for that
   version; the UI reports it as incomplete. The pack is reported as failed.

`Uninstall`: read marker, delete listed files, prune empty directories under the install dir,
delete the marker. `ExpandDependencies` prepends `depends_on` ids that are neither installed nor
already in the list, recursively (the table has depth one today).

Cancellation: checked before each step and inside the download and extraction loops; a cancelled
pack is reported as cancelled and the remaining packs are not started.

## 7. Qt dialog

Files: `pcsx2-qt/ShaderPackDownloadDialog.{h,cpp,ui}`. Opened by a new
`QPushButton shaderChainDownload` ("Download Shader Packs...") in the Shader Chain group of
`GraphicsPostProcessingSettingsTab.ui`, enabled with the group; after `exec()` returns the tab
calls `populateShaderChainPresets(dialog()->isPerGameSettings())`.

Layout: `QTableWidget packs` (columns: checkbox+name, description, licence, status),
`QLabel note` ("Third-party content under its own licences. Retro Crisis requires the libretro
pack and selects it automatically."), `QLabel status`, `QProgressBar progress`, buttons
`install` (default), `uninstall`, `close`.

Behaviour:
- On open, a worker resolve pass fills the status column (statuses from section 5); rows in
  NotInstalled or UpdateAvailable state start checked. A marker with an empty version (section 6
  step 6) shows "Installed (incomplete, reinstall recommended)" and also starts checked.
- Install: collect checked ids → `ShaderPacks::ExpandDependencies` → show the expanded list in the
  status label → start `ShaderPackWorker : QtAsyncProgressThread` whose `runAsync()` calls
  `ShaderPacks::Install(ids, this)`. While running: Install becomes Cancel (`requestInterruption`
  + join), Close and Uninstall disabled, progress bar bound to `progressUpdated`, label to
  `statusUpdated`.
- Uninstall: confirmation box listing the checked installed packs; when the libretro pack is being
  removed while the installed Retro Crisis pack is not, the box appends "The Retro Crisis presets
  require the libretro slang shaders and will stop working.". Then the worker runs `Uninstall` for
  each.
- On `threadFinished`: re-read markers, refresh statuses, re-enable buttons; the final status text
  is "Done.", "Uninstalled.", "Cancelled.", or "Completed with errors: <pack names>".
- Every exit path (`QDialog::done` override, so buttons, Escape and the window close button alike)
  interrupts and joins the worker before the dialog can be destroyed, then reports "Cancelled.";
  destroying a running `QThread` is fatal in Qt.
- The dialog holds a `std::unique_ptr<HTTPDownloader>` only inside the worker; nothing is written
  to the INI.

## 8. Error handling summary

| Condition | Behaviour |
|---|---|
| Offline / API error at resolve | status "could not check"; Install skips that pack with the message |
| Download cancelled | nothing on disk changed; "Cancelled" |
| HTTP non-200, empty or corrupt zip | pack skipped; existing install untouched |
| Path traversal or write failure during extract | pack aborted; marker records written files; error surfaced |
| Marker missing/malformed | NotInstalled; a fresh install overwrites files in place (no deletion pass) |
| Dependency install fails | dependent pack is skipped with "requires <name>" |

## 9. Testing

Unit tests `tests/ctest/core/shader_packs_tests.cpp` and `tests/ctest/core/shader_pack_archive_tests.cpp`
(gtest; zips are created in the test with libzip's writer API into a temp directory, no committed
binaries, no network):
- Entry names: strip count honoured; empty and `.` segments dropped (`top/./crt/a.slangp` →
  `crt/a.slangp`); `__MACOSX` in any segment and `.DS_Store` skipped; directory entries and
  strip-to-nothing entries skipped; `../x`, `/abs`, backslash and drive-letter entries rejected.
- Extraction: nested directories created; written list in order; a rejected entry aborts the pack
  and keeps the files already written (the abort semantics of section 6); the per-entry and running
  total size caps abort with the files written so far intact (the caps are injectable so the test
  does not need a huge archive).
- Marker: write/read round-trip, including an empty version (the incomplete case) and an empty file
  list; unsafe paths in `files` dropped; missing `version`, missing and malformed files yield
  nullopt.
- `ParseReleaseJson`: picks the `.zip` asset, honours `asset_exclude`, errors on no match or
  malformed JSON. `ParseCommitJson`: yields the SHA; errors on malformed input.
- `ExpandDependencies` with and without an installed dependency marker, including a marker whose
  files were deleted by hand (still counts as installed).
- `Uninstall`: deletes exactly the listed files, prunes empty dirs, keeps unrelated files, removes
  the marker, and works for a marker with no files.

Manual (Mac, then Windows over SSH): install all three from a data dir without markers; re-open
shows Installed; uninstall satpixie removes only its five files; installing RetroCrisis alone
after removing both auto-adds the libretro pack; cancel mid-download leaves no partial files;
offline shows "could not check" and Install reports the error; on Windows a downloaded
RetroCrisis preset loads on D3D12.

Success criterion: a fresh PCSX2 data directory reaches a working RetroCrisis preset on both
machines using only the dialog, and the unit tests pass on both.
