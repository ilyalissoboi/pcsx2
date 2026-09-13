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
