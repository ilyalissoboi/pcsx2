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
