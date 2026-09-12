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
	// Path::IsAbsolute only recognises drive letters on Windows, but we need to reject them
	// on all platforms because INI files can be copied between machines.
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
