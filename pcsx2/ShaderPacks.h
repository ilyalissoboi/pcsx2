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
