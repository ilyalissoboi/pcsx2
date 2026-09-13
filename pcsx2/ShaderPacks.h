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
	/// progress may be null; when it is not, it is attached to the request and cancelling it aborts
	/// the wait. Note that the downloader's timeout covers total elapsed time, so callers that also
	/// download should set a short timeout around this call.
	std::optional<ResolvedVersion> ResolveLatest(const PackInfo& pack, HTTPDownloader& http, ProgressCallback* progress, Error* error);

	/// Install order for the requested packs: a missing dependency is inserted before its dependent;
	/// duplicates and unknown ids are dropped. Only one dependency level is resolved; the pack table
	/// has single-level dependencies.
	std::vector<std::string> ExpandDependencies(const std::string& shaders_root, std::span<const std::string> ids);

	/// Deletes the files recorded in the pack's marker, prunes directories left empty under the pack's
	/// install directory, and removes the marker. Fails if the pack is not installed.
	bool Uninstall(const std::string& shaders_root, std::string_view id, Error* error);

	/// Removes empty directories below and including dir, deepest first.
	void PruneEmptyDirectories(const std::string& dir);

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

} // namespace ShaderPacks
