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
