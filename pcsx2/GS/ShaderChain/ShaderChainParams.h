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
	/// Backends only set the parameters in the list; a freshly built chain starts from the preset
	/// defaults, so this is complete after a preset change. The editor dialog pushes every
	/// parameter explicitly so that resetting one takes effect on a live chain.
	void ApplyOverridesToStore(std::string_view preset_relative_path);

	/// Spin-box decimals for a parameter step: 1 -> 0, 0.5 -> 1, 0.05 -> 2, 0.01 -> 2, 0.001 -> 3,
	/// 0.0001 -> 4, 1e-6 -> 4 (capped), 16 -> 0, 100 -> 0, step <= 0 -> 3.
	int DecimalsForStep(float step);

	/// True when `value` equals the preset default within a small relative epsilon
	/// (1e-6 * max(1, |initial|)). Used to decide which parameters are persisted.
	bool IsDefaultValue(float value, float initial);

	/// Next (forward) or previous entry of `favorites` relative to `current`, wrapping around and
	/// skipping entries whose file does not exist under the shaders root. If `current` is not in
	/// the list, forward returns the first existing entry and backward the last. Empty if none exist.
	std::string NextFavorite(const std::vector<std::string>& favorites, std::string_view current, bool forward);
	std::string NextFavoriteIn(const std::string& shaders_root, const std::vector<std::string>& favorites,
		std::string_view current, bool forward);
} // namespace ShaderChainParams
