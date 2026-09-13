# Design: Shader parameters, favourites and hotkeys (phase 3)

Date: 2026-09-13
Status: approved in brainstorm, awaiting implementation plan
Depends on: phase 1 (`2026-09-12-librashader-shader-chain-design.md`), phase 2 (`2026-09-13-shader-pack-downloader-design.md`)

## 1. Goal and scope

Let users tune the `#pragma parameter` values of the active `.slangp` preset, persist those values per preset (globally or per game), switch presets while playing with hotkeys, and see the active preset in the settings overlay.

In scope:

- A parameter editor dialog opened from the Post-Processing tab, with live apply while a game runs.
- Persistence of changed parameters per preset in the INI, in the global layer or the per-game layer.
- A user-curated, ordered, global favourites list edited from the Post-Processing tab.
- Three hotkeys: Toggle Shader Chain, Next Shader Preset, Previous Shader Preset.
- One entry in the settings overlay.

Out of scope: parameter presets or A/B comparison, sharing parameter files with RetroArch, editing `#pragma parameter` defaults in preset files, per-favourite hotkeys, OpenGL support (still greyed out).

## 2. Architecture

```
Qt (UI thread)                         Core (CPU thread)                     GS thread
ShaderParametersDialog ──edit──▶ ShaderPresets::Params().Set()  ◀── ShaderChainParams::ApplyOverridesToStore()
        │                                  ▲                                    │  drained by backends
        └──debounced──▶ INI list ──────────┘ (VMManager::ApplySettings, hotkeys)  ▼
ShaderFavoritesDialog ──▶ INI list ◀── hotkeys NextShaderPreset/PreviousShaderPreset
```

One new core module, `pcsx2/GS/ShaderChain/ShaderChainParams.{h,cpp}`, owns everything non-visual: enumeration of a preset's parameters through librashader, parsing and formatting of persisted overrides, pushing overrides into the phase 1 `ShaderPresets::ParameterStore`, and favourites stepping. Two new Qt dialogs and two buttons on the Post-Processing tab use it. Hotkeys live in `g_gs_hotkeys` in `pcsx2/GS/GS.cpp` and use the same module. Backends are unchanged: they already drain the store when its generation changes and the store's preset matches `GSConfig.ShaderChainPreset`, and they reset their last-seen generation when they rebuild a chain, so a fresh chain picks up the current snapshot on its first frame.

## 3. Settings keys

| Key | Layer | Format |
|---|---|---|
| `[ShaderChainParams]` `<preset relative path>` | global or per game | string list; entries `name=value`, one per parameter changed from the preset default |
| `[EmuCore/GS]` `ShaderChainFavorites` | global only | string list of preset relative paths, order preserved |

- The key for a preset's parameters is the same relative path stored in `ShaderChainPreset` (forward slashes, e.g. `shaders_slang/crt/crt-royale.slangp`). SimpleIni accepts slashes, spaces and dots in key names; the path never contains `=` because preset file names in the supported packs do not, and `ResolvePresetPath` already rejects `..` components.
- Values are written with `%g`-style shortest round-trip formatting (`fmt::format("{}", value)`) and parsed with `StringUtil::FromChars<float>`. Malformed entries (no `=`, empty name, non-numeric value) are skipped with a Console warning. Duplicate names keep the last entry.
- Only parameters whose value differs from the preset default are stored, so an updated preset that changes a default takes effect for untouched parameters.
- Lists are read through the layered settings, so a per-game key replaces the global key wholesale. There is no per-entry merge; section 6 describes how the dialog handles that.
- No files are written into the shaders folder.

## 4. Core module `ShaderChainParams`

```cpp
namespace ShaderChainParams
{
	struct ParameterInfo
	{
		std::string name;
		std::string description;
		float initial, minimum, maximum, step;
	};

	/// Loads the preset with librashader (no runtime, no GPU) and lists its #pragma parameters.
	/// Safe on any thread. Returns false with a non-empty error when the library is unavailable
	/// or the preset fails to parse.
	bool EnumerateParameters(const std::string& absolute_preset_path, std::vector<ParameterInfo>* out, Error* error);

	using ParamList = ShaderPresets::ParameterStore::ParamList;

	/// "name=value" entries -> list. Skips malformed entries with a warning; last duplicate wins.
	ParamList ParseOverrides(const std::vector<std::string>& entries);
	std::vector<std::string> FormatOverrides(const ParamList& params);

	/// Settings key under [ShaderChainParams] for a preset relative path (currently the path itself).
	const char* SettingsSection(); // "ShaderChainParams"

	/// Reads the layered override list for the preset and pushes it into ShaderPresets::Params().
	/// An empty or missing list pushes an empty ParamList, which resets the chain to defaults.
	void ApplyOverridesToStore(std::string_view preset_relative_path);

	/// Next (forward) or previous entry of the favourites list relative to `current`, wrapping
	/// around, skipping entries whose resolved file does not exist. If `current` is not in the
	/// list, forward returns the first existing entry and backward the last. Empty if none exist.
	std::string NextFavorite(const std::vector<std::string>& favorites, std::string_view current, bool forward);
	std::string NextFavoriteIn(const std::string& shaders_root, const std::vector<std::string>& favorites,
		std::string_view current, bool forward);
}
```

`EnumerateParameters` uses the loader's `ShaderChain::Common()` table: `preset_ctx_create`, `preset_ctx_set_core_name("PCSX2")`, `preset_create_with_options` (options struct with the API version, no runtime set, so the same preset enumerates identically on every backend), `preset_get_runtime_params`, copy into `ParameterInfo`, then `preset_free_runtime_params`, `preset_free`, `preset_ctx_free`. Errors are described with the existing `ShaderChain::DescribeAndFreeError`. When `ShaderChain::GetAvailability().available` is false the function returns false with the availability reason.

`ApplyOverridesToStore` calls `Host::GetStringListSetting(SettingsSection(), preset)` and `ShaderPresets::Params().Set(std::string(preset), ParseOverrides(entries))`. It is the single point that turns persisted values into live ones.

`NextFavoriteIn` exists so the file-existence check can be unit-tested against a temporary directory; `NextFavorite` forwards with `EmuFolders::Shaders`.

## 5. Apply flow, hotkeys and overlay

### 5.1 When overrides reach the GPU

- `VMManager::ApplySettings()` (CPU thread, runs on every settings apply, VM start and per-game reload) calls `ShaderChainParams::ApplyOverridesToStore(EmuConfig.GS.ShaderChainPreset)` after `LoadSettings()`. The call is unconditional; re-pushing an unchanged list costs a handful of `set_param` calls on the next frame.
- The editor dialog pushes on the UI thread after each edit. The store is mutex-protected and the backends compare the snapshot's preset against the live one, so a push for a preset that is not active is ignored.
- Ordering with chain rebuilds needs no extra work: a rebuilt chain resets the backend's last-seen generation, so the current snapshot is applied on the first frame of the new chain.

### 5.2 Hotkeys

Three new entries in `g_gs_hotkeys` (`pcsx2/GS/GS.cpp`), category `Graphics`, following the `CycleTVShader` pattern (`if (pressed) return;`, `Host::AddKeyedOSDMessage`, write `EmuConfig.GS`, mirror to `GSConfig` with `MTGS::RunOnGSThread`). They appear automatically in the Hotkeys settings page through `InputManager::GetHotkeyList()`. All three are runtime-only and do not write the INI, like the existing graphics hotkeys.

| Hotkey | Behaviour | OSD |
|---|---|---|
| `ToggleShaderChain` | Flips `EmuConfig.GS.ShaderChainEnabled`, mirrors it to `GSConfig`. | `Shader chain enabled.` / `Shader chain disabled.` |
| `NextShaderPreset` | Reads `EmuCore/GS/ShaderChainFavorites` via `Host::GetStringListSetting`, calls `NextFavorite(favorites, EmuConfig.GS.ShaderChainPreset, true)`. On a hit: sets `EmuConfig.GS.ShaderChainPreset`, sets `EmuConfig.GS.ShaderChainEnabled = true` if it was off, mirrors both to `GSConfig`, calls `ApplyOverridesToStore(new preset)`. | `Shader preset: <file stem>.` or `No shader presets in favourites list.` |
| `PreviousShaderPreset` | Same with `forward = false`. | Same |

Favourites whose file is missing are skipped silently by `NextFavorite`; the hotkey logs one Console warning listing the skipped entries. The OSD key is `ShaderChainHotkey` for all three so consecutive presses replace one message.

### 5.3 Settings overlay

`ImGuiManager::DrawSettingsOverlay` (`pcsx2/ImGui/ImGuiOverlays.cpp`) appends `SC=<stem> ` in the hardware-renderer block when `GSConfig.ShaderChainEnabled` is set and `GSConfig.ShaderChainPreset` is non-empty, where `<stem>` is the preset file name without extension, truncated to 32 characters with a trailing `…`. This matches the overlay's compact `IR=` / `BL=` style.

## 6. Qt UI

### 6.1 Post-Processing tab

`pcsx2-qt/Settings/GraphicsPostProcessingSettingsTab.ui`, Shader Chain group: a new `QHBoxLayout` row between the Use Global/Open Folder/Download row and the status label containing `QPushButton shaderChainParameters` ("Parameters...") and `QPushButton shaderChainFavorites` ("Favorites..."), followed by a stretch spacer. Tab stops are added after `shaderChainDownload`.

`GraphicsSettingsWidget`:

- `shaderChainParameters` is enabled when the chain group is enabled, the chain is enabled, and the effective preset (`getEffectiveStringValue("EmuCore/GS", "ShaderChainPreset")`) is non-empty. `updateShaderChainPresetDisplay()` and `onShaderChainEnabledChanged()` maintain the state.
- `shaderChainFavorites` is enabled when the chain group is enabled; in per-game settings it is disabled with the tooltip "Favourites are shared by all games and can be edited in the global settings."
- New slots `onShaderChainParametersClicked()` (opens `ShaderParametersDialog(dialog(), this, effective preset)`) and `onShaderChainFavoritesClicked()` (opens `ShaderFavoritesDialog(this, effective preset)`).
- `registerWidgetHelp` entries for both buttons.

### 6.2 `ShaderParametersDialog`

Files: `pcsx2-qt/ShaderParametersDialog.{h,cpp,ui}`. Modal. Constructor: `ShaderParametersDialog(SettingsWindow* settings, QWidget* parent, std::string preset_relative_path)`.

Loading:

1. Title `Shader Parameters - <file stem>`.
2. Resolve the absolute path with `ShaderPresets::ResolvePresetPath`; call `EnumerateParameters`. On failure the body shows a single label with the error text and only `Close` is enabled. Zero parameters shows "This preset has no adjustable parameters."
3. Read the effective override list: per-game window uses `settings->getSettingsInterface()->GetStringList(section, key)` when the key exists there, otherwise `Host::GetBaseStringListSetting`; global window uses the base list. Parse with `ParseOverrides`. Persisted values outside the parameter's range are clamped for display only; the INI is left untouched until the row is edited.

Body: a `QScrollArea` over a `QGridLayout`, one row per parameter:

| Column | Widget | Behaviour |
|---|---|---|
| 0 | `QLabel` description (falls back to name when empty) | tooltip shows the raw parameter name |
| 1 | `QSlider` horizontal | integer positions `0..N`, `N = min(round((max-min)/step), 10000)`; position `p` maps to `min + p*step` |
| 2 | `QDoubleSpinBox` | range `[min,max]`, `singleStep = step`, decimals = digits needed for `step` (1 gives 0, 0.5 gives 1, 0.01 gives 2, capped at 4) |
| 3 | `QPushButton` "Reset" | enabled only while the value differs from `initial`; sets the value to `initial` |

Slider and spin box update each other behind a re-entrancy guard. Parameters with `step <= 0` or `max <= min` show only the spin box with an unbounded range, no slider.

Footer: `Reset All` (per-game window: `Use Global Settings`), `Close`.

Applying and persisting:

- Every value change rebuilds the override list (entries whose value equals `initial` within `step/2` are dropped), pushes it to `ShaderPresets::Params().Set(preset, list)` immediately, and restarts a 250 ms single-shot `QTimer`.
- The timer writes the list: global window `Host::SetBaseStringListSettingValue(section, key, entries)` then `Host::CommitBaseSettingChanges()`; per-game window `getSettingsInterface()->SetStringList(...)` then `settings->saveAndReloadGameSettings()`. An empty list removes the key (`DeleteValue` / `RemoveBaseSettingValue`) instead of writing an empty list. After writing, the dialog pushes the store once more so a settings apply that raced the write cannot leave stale values live.
- `Close` and `reject()` flush a pending write first.
- Per-game semantics: a per-game list replaces the global list wholesale, so the first per-game edit writes the full effective list into the per-game key. `Use Global Settings` removes the per-game key, reloads, re-reads the effective (global) list and refreshes every row. `Reset All` in the global window sets every row to `initial`, which removes the key.

### 6.3 `ShaderFavoritesDialog`

Files: `pcsx2-qt/ShaderFavoritesDialog.{h,cpp,ui}`. Modal, global only. Constructor: `ShaderFavoritesDialog(QWidget* parent, std::string current_preset)`.

- A `QListWidget` listing `ShaderChainFavorites` in order, one relative path per row. Rows whose resolved file does not exist are italic with tooltip "File not found".
- Buttons: `Add Current` (adds `current_preset`; disabled when empty or already listed), `Add...` (opens `ShaderPresetPickerDialog`, adds the selection unless already listed), `Remove`, `Move Up`, `Move Down`, `Close`. `Remove`, `Move Up` and `Move Down` follow the selection and list bounds.
- Every change writes the whole list with `Host::SetBaseStringListSettingValue("EmuCore/GS", "ShaderChainFavorites", ...)` and `Host::CommitBaseSettingChanges()`; an empty list removes the key. No debounce; these are single clicks.

## 7. Error handling summary

| Situation | Behaviour |
|---|---|
| Malformed override entry | skipped with Console warning; rest of the list applies |
| Unknown parameter name in list | passed to the store; librashader `set_param` rejects it, the backend logs and continues (existing behaviour) |
| Persisted value out of range | dialog clamps for display; INI unchanged until edited |
| Preset fails to enumerate | dialog shows the error text; hotkeys unaffected (they never call librashader) |
| librashader unavailable | Parameters and Favorites buttons disabled with the group (existing gating) |
| Favourite file missing | skipped by hotkeys with one Console warning; italic in the dialog; never removed automatically |
| Empty or all-missing favourites | OSD "No shader presets in favourites list." |

## 8. Testing

Unit tests in `tests/ctest/core/shader_chain_params_tests.cpp` (no GPU, no Qt), registered next to `shader_presets_tests.cpp`:

- `ParseOverrides`: round trip through `FormatOverrides`; skips entries without `=`, with an empty name, or with a non-numeric value; last duplicate wins; accepts negative and exponent notation.
- `NextFavoriteIn` with a temporary shaders root: forward and backward from a listed entry; wrap-around at both ends; current not in list (forward gives first, backward gives last); skips entries whose file is missing; empty list and all-missing return empty.
- `EnumerateParameters`: when `ShaderChain::GetAvailability().available`, a fixture preset (`.slangp` referencing a `.slang` with one `#pragma parameter` line) returns name, description, initial, minimum, maximum and step; a preset referencing a missing shader returns false with a non-empty error. Skipped with `GTEST_SKIP()` when the library is not loadable.

Manual acceptance on both machines (Metal and Vulkan on macOS; D3D12 and one of D3D11/Vulkan on Windows):

1. Open Parameters on a preset with several parameters (e.g. `crt-royale`); drag a slider with a game running and see the picture change immediately.
2. Close and reopen the dialog; the value is persisted. Per-row Reset re-enables and disables correctly; Reset All clears the INI key.
3. Per-game: edit a value, confirm the per-game INI has the key, `Use Global Settings` removes it and the global value returns.
4. Favourites: add current, add via picker, reorder, remove; entries survive a restart.
5. Hotkeys: bind Toggle, Next and Previous; each shows an OSD message; Next with the chain disabled enables it; a favourite pointing at a deleted file is skipped.
6. Overlay: with `OsdShowSettings` on, `SC=<stem>` appears while the chain is enabled and disappears when toggled off.
