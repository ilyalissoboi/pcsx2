// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderChainParams.h"
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "common/Error.h"
#include "common/SettingsInterface.h"
#include "Config.h"
#include "Host.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/StringUtil.h"

#include "fmt/format.h"
#include "fmt/ranges.h" // fmt::join

#include <algorithm>
#include <cmath>

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
		if (name.empty())
		{
			WARNING_LOG("ShaderChainParams: ignoring override '{}' (empty name).", entry);
			continue;
		}

		const std::optional<float> value = StringUtil::FromChars<float>(value_str);
		if (!value.has_value())
		{
			WARNING_LOG("ShaderChainParams: ignoring override '{}' (non-numeric value).", entry);
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

int ShaderChainParams::DecimalsForStep(float step)
{
	if (!(step > 0.0f))
		return 3;
	// Small bias so 0.01f (slightly below 0.01) still yields 2, not 3.
	const double digits = std::ceil(-std::log10(static_cast<double>(step)) - 1e-6);
	return std::clamp(static_cast<int>(digits), 0, 4);
}

bool ShaderChainParams::IsDefaultValue(float value, float initial)
{
	const float tolerance = 1e-6f * std::max(1.0f, std::abs(initial));
	return std::abs(value - initial) <= tolerance;
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

SettingsInterface* ShaderChainParams::PersistActivePresetIn(SettingsInterface* base, SettingsInterface* game, const std::string& preset)
{
	SettingsInterface* const target = (game && game->ContainsValue("EmuCore/GS", "ShaderChainPreset")) ? game : base;
	if (!target)
		return nullptr;

	target->SetStringValue("EmuCore/GS", "ShaderChainPreset", preset.c_str());
	target->SetBoolValue("EmuCore/GS", "ShaderChainEnabled", true);
	return target;
}

void ShaderChainParams::PersistActivePreset(const std::string& preset)
{
	bool base_changed = false;
	{
		auto lock = Host::GetSettingsLock();
		SettingsInterface* const base = Host::Internal::GetBaseSettingsLayer();
		SettingsInterface* const game = Host::Internal::GetGameSettingsLayer();
		SettingsInterface* const target = PersistActivePresetIn(base, game, preset);
		if (!target)
			return;

		if (target == game)
		{
			Error error;
			if (!game->Save(&error))
				WARNING_LOG("ShaderChain: failed to save per-game settings: {}", error.GetDescription());
		}
		else
		{
			base_changed = true;
		}
	}
	// Commit outside the lock: the host implementation takes the settings lock itself.
	if (base_changed)
		Host::CommitBaseSettingChanges();
}
