// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPacks.h"
#include <algorithm>

#include "Config.h"
#include "Host.h"
#include "ShaderPackArchive.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/HTTPDownloader.h"
#include "common/ProgressCallback.h"
#include "common/StringUtil.h"
#include "common/ZipHelpers.h"

#include "fmt/chrono.h"
#include "fmt/format.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <chrono>
#include <ctime>
#include <thread>

namespace
{
	constexpr ShaderPacks::PackInfo s_packs[] = {
		{"shaders_slang", "libretro slang shaders", "The RetroArch slang shader collection (CRT, NTSC, scalers, and more). Required by the Retro Crisis presets.",
			"Mixed per-shader licences, see each file", "libretro/slang-shaders", ShaderPacks::VersionSource::BranchHead, "master", nullptr, 1,
			"shaders_slang", nullptr},
		{"retro-crisis-gdv-ntsc", "Retro Crisis GDV-NTSC presets", "CRT presets based on Guest Advanced NTSC, organised by display resolution.",
			"GPL-3.0", "RetroCrisis/Retro-Crisis-GDV-NTSC", ShaderPacks::VersionSource::LatestRelease, nullptr, nullptr, 1,
			"shaders_slang/retro crisis", "shaders_slang"},
		{"satpixie-crt", "satpixie CRT shader", "A lightweight CRT shader with ghosting and chroma options.",
			"See repository licence", "Conkwer/satpixie-crt-shader", ShaderPacks::VersionSource::LatestRelease, nullptr, "variants", 4,
			"shaders_slang", nullptr},
	};

	constexpr const char* MARKER_DIR = ".shaderpacks";

	bool IsSafeRelativePath(std::string_view rel)
	{
		if (rel.empty() || rel[0] == '/' || rel[0] == '\\')
			return false;
		if (rel.size() >= 2 && rel[1] == ':')
			return false;
		if (rel.find('\\') != std::string_view::npos)
			return false;

		// Check for ".." segments
		size_t pos = 0;
		while (pos < rel.size())
		{
			const size_t next = rel.find('/', pos);
			const size_t segment_len = (next == std::string_view::npos) ? (rel.size() - pos) : (next - pos);
			const std::string_view segment = rel.substr(pos, segment_len);
			if (segment == "..")
				return false;
			pos = (next == std::string_view::npos) ? rel.size() : next + 1;
		}
		return true;
	}
} // namespace

std::span<const ShaderPacks::PackInfo> ShaderPacks::GetPacks()
{
	return s_packs;
}

const ShaderPacks::PackInfo* ShaderPacks::FindPack(std::string_view id)
{
	for (const PackInfo& pack : s_packs)
	{
		if (id == pack.id)
			return &pack;
	}
	return nullptr;
}

std::string ShaderPacks::GetMarkerPath(const std::string& shaders_root, std::string_view id)
{
	return Path::Combine(Path::Combine(shaders_root, MARKER_DIR), fmt::format("{}.json", id));
}

std::optional<ShaderPacks::InstalledPack> ShaderPacks::ReadMarker(const std::string& shaders_root, std::string_view id)
{
	const std::string path = GetMarkerPath(shaders_root, id);
	std::optional<std::string> text = FileSystem::ReadFileToString(path.c_str());
	if (!text.has_value())
		return std::nullopt;

	rapidjson::Document doc;
	doc.Parse(text->data(), text->size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		WARNING_LOG("ShaderPacks: marker {} is not valid JSON, treating pack as not installed.", path);
		return std::nullopt;
	}

	const auto get_string = [&doc](const char* key, std::string* out) {
		const auto it = doc.FindMember(key);
		if (it == doc.MemberEnd() || !it->value.IsString())
			return false;
		out->assign(it->value.GetString(), it->value.GetStringLength());
		return true;
	};

	InstalledPack pack;
	const auto files = doc.FindMember("files");
	if (!get_string("id", &pack.id) || !get_string("version", &pack.version) || files == doc.MemberEnd() || !files->value.IsArray())
	{
		WARNING_LOG("ShaderPacks: marker {} is missing required fields, treating pack as not installed.", path);
		return std::nullopt;
	}
	get_string("source_url", &pack.source_url);
	get_string("installed_at", &pack.installed_at);
	for (const rapidjson::Value& v : files->value.GetArray())
	{
		if (v.IsString())
		{
			const std::string_view entry(v.GetString(), v.GetStringLength());
			if (IsSafeRelativePath(entry))
				pack.files.emplace_back(entry);
			else
				WARNING_LOG("ShaderPacks: marker {} lists unsafe path '{}', ignoring it.", path, entry);
		}
	}
	return pack;
}

bool ShaderPacks::WriteMarker(const std::string& shaders_root, const InstalledPack& pack, Error* error)
{
	const std::string dir = Path::Combine(shaders_root, MARKER_DIR);
	if (!FileSystem::CreateDirectoryPath(dir.c_str(), true, error))
		return false;

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	writer.StartObject();
	writer.Key("id");
	writer.String(pack.id.c_str(), static_cast<rapidjson::SizeType>(pack.id.size()));
	writer.Key("version");
	writer.String(pack.version.c_str(), static_cast<rapidjson::SizeType>(pack.version.size()));
	writer.Key("source_url");
	writer.String(pack.source_url.c_str(), static_cast<rapidjson::SizeType>(pack.source_url.size()));
	writer.Key("installed_at");
	writer.String(pack.installed_at.c_str(), static_cast<rapidjson::SizeType>(pack.installed_at.size()));
	writer.Key("files");
	writer.StartArray();
	for (const std::string& file : pack.files)
		writer.String(file.c_str(), static_cast<rapidjson::SizeType>(file.size()));
	writer.EndArray();
	writer.EndObject();

	const std::string path = GetMarkerPath(shaders_root, pack.id);
	if (!FileSystem::WriteBinaryFile(path.c_str(), buffer.GetString(), buffer.GetSize()))
	{
		Error::SetStringFmt(error, "Failed to write marker {}", path);
		return false;
	}
	return true;
}

bool ShaderPacks::RemoveMarker(const std::string& shaders_root, std::string_view id)
{
	const std::string path = GetMarkerPath(shaders_root, id);
	return !FileSystem::FileExists(path.c_str()) || FileSystem::DeleteFilePath(path.c_str());
}

std::optional<ShaderPacks::InstalledPack> ShaderPacks::GetInstalled(std::string_view id)
{
	return ReadMarker(EmuFolders::Shaders, id);
}

std::string ShaderPacks::CurrentTimestamp()
{
	const std::time_t now = std::time(nullptr);
	return fmt::format("{:%Y-%m-%dT%H:%M:%SZ}", fmt::gmtime(now));
}

bool ShaderPacks::ParseCommitJson(std::string_view json, std::string* sha, Error* error)
{
	rapidjson::Document doc;
	doc.Parse(json.data(), json.size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		Error::SetStringView(error, "Commit response is not valid JSON.");
		return false;
	}
	const auto it = doc.FindMember("sha");
	if (it == doc.MemberEnd() || !it->value.IsString() || it->value.GetStringLength() == 0)
	{
		Error::SetStringView(error, "Commit response has no sha field.");
		return false;
	}
	sha->assign(it->value.GetString(), it->value.GetStringLength());
	return true;
}

bool ShaderPacks::ParseReleaseJson(std::string_view json, const char* asset_exclude, ResolvedVersion* out, Error* error)
{
	rapidjson::Document doc;
	doc.Parse(json.data(), json.size());
	if (doc.HasParseError() || !doc.IsObject())
	{
		Error::SetStringView(error, "Release response is not valid JSON.");
		return false;
	}

	const auto tag = doc.FindMember("tag_name");
	if (tag == doc.MemberEnd() || !tag->value.IsString())
	{
		Error::SetStringView(error, "Release response has no tag_name field.");
		return false;
	}

	const auto assets = doc.FindMember("assets");
	if (assets != doc.MemberEnd() && assets->value.IsArray())
	{
		for (const rapidjson::Value& asset : assets->value.GetArray())
		{
			const auto name = asset.FindMember("name");
			const auto url = asset.FindMember("browser_download_url");
			if (name == asset.MemberEnd() || !name->value.IsString() || url == asset.MemberEnd() || !url->value.IsString())
				continue;

			const std::string_view name_sv(name->value.GetString(), name->value.GetStringLength());
			if (!name_sv.ends_with(".zip"))
				continue;
			if (asset_exclude && name_sv.find(asset_exclude) != std::string_view::npos)
				continue;

			out->version.assign(tag->value.GetString(), tag->value.GetStringLength());
			out->download_url.assign(url->value.GetString(), url->value.GetStringLength());
			return true;
		}
	}

	Error::SetStringFmt(error, "Release {} has no matching .zip asset.", std::string_view(tag->value.GetString(), tag->value.GetStringLength()));
	return false;
}

std::string ShaderPacks::GetVersionUrl(const PackInfo& pack)
{
	if (pack.version_source == VersionSource::BranchHead)
		return fmt::format("https://api.github.com/repos/{}/commits/{}", pack.github_repo, pack.branch);
	return fmt::format("https://api.github.com/repos/{}/releases/latest", pack.github_repo);
}

std::optional<ShaderPacks::ResolvedVersion> ShaderPacks::ResolveLatest(const PackInfo& pack, HTTPDownloader& http, Error* error)
{
	const std::string url = GetVersionUrl(pack);
	s32 status = 0;
	std::string body;
	http.CreateRequest(url, [&status, &body](s32 status_code, const std::string&, HTTPDownloader::Request::Data data) {
		status = status_code;
		body.assign(reinterpret_cast<const char*>(data.data()), data.size());
	});
	http.WaitForAllRequests();

	if (status != HTTPDownloader::HTTP_STATUS_OK)
	{
		Error::SetStringFmt(error, "Version check for {} failed (HTTP {}).", pack.display_name, status);
		return std::nullopt;
	}

	ResolvedVersion out;
	if (pack.version_source == VersionSource::BranchHead)
	{
		std::string sha;
		if (!ParseCommitJson(body, &sha, error))
			return std::nullopt;
		out.version = sha.substr(0, 12);
		out.download_url = fmt::format("https://github.com/{}/archive/{}.zip", pack.github_repo, sha);
	}
	else if (!ParseReleaseJson(body, pack.asset_exclude, &out, error))
	{
		return std::nullopt;
	}
	return out;
}

std::vector<std::string> ShaderPacks::ExpandDependencies(const std::string& shaders_root, std::span<const std::string> ids)
{
	std::vector<std::string> result;
	const auto add = [&result](std::string_view id) {
		if (std::find(result.begin(), result.end(), id) == result.end())
			result.emplace_back(id);
	};

	for (const std::string& id : ids)
	{
		const PackInfo* pack = FindPack(id);
		if (!pack)
		{
			WARNING_LOG("ShaderPacks: ignoring unknown pack id '{}'.", id);
			continue;
		}
		if (pack->depends_on && FindPack(pack->depends_on) && !ReadMarker(shaders_root, pack->depends_on).has_value())
			add(pack->depends_on);
		add(id);
	}
	return result;
}

void ShaderPacks::PruneEmptyDirectories(const std::string& dir)
{
	if (!FileSystem::DirectoryExists(dir.c_str()))
		return;

	FileSystem::FindResultsArray dirs;
	FileSystem::FindFiles(dir.c_str(), "*", FILESYSTEM_FIND_RECURSIVE | FILESYSTEM_FIND_FOLDERS | FILESYSTEM_FIND_HIDDEN_FILES, &dirs);

	// Deepest first, so parents become empty after their children are removed.
	std::sort(dirs.begin(), dirs.end(), [](const FILESYSTEM_FIND_DATA& a, const FILESYSTEM_FIND_DATA& b) {
		return a.FileName.size() > b.FileName.size();
	});
	for (const FILESYSTEM_FIND_DATA& fd : dirs)
	{
		if (FileSystem::DirectoryIsEmpty(fd.FileName.c_str()))
			FileSystem::DeleteDirectory(fd.FileName.c_str());
	}
	if (FileSystem::DirectoryIsEmpty(dir.c_str()))
		FileSystem::DeleteDirectory(dir.c_str());
}

bool ShaderPacks::Uninstall(const std::string& shaders_root, std::string_view id, Error* error)
{
	const PackInfo* pack = FindPack(id);
	if (!pack)
	{
		Error::SetStringFmt(error, "Unknown shader pack '{}'.", id);
		return false;
	}

	const std::optional<InstalledPack> installed = ReadMarker(shaders_root, id);
	if (!installed.has_value())
	{
		Error::SetStringFmt(error, "{} is not installed.", pack->display_name);
		return false;
	}

	for (const std::string& rel : installed->files)
	{
		const std::string path = Path::Combine(shaders_root, rel);
		if (FileSystem::FileExists(path.c_str()) && !FileSystem::DeleteFilePath(path.c_str()))
			WARNING_LOG("ShaderPacks: failed to delete '{}'.", path);
	}

	PruneEmptyDirectories(Path::Combine(shaders_root, pack->install_subdir));

	if (!RemoveMarker(shaders_root, id))
	{
		Error::SetStringFmt(error, "Failed to remove the marker for {}.", pack->display_name);
		return false;
	}
	INFO_LOG("ShaderPacks: uninstalled {} ({} files).", pack->display_name, installed->files.size());
	return true;
}

namespace
{
	constexpr float DOWNLOAD_TIMEOUT_SECONDS = 600.0f; // HTTPDownloader's timeout is total elapsed time

	bool DownloadToMemory(HTTPDownloader& http, const std::string& url, ProgressCallback* progress, std::vector<u8>* out, Error* error)
	{
		bool done = false;
		s32 status = 0;
		http.CreateRequest(url, [&done, &status, out](s32 status_code, const std::string&, HTTPDownloader::Request::Data data) {
			status = status_code;
			*out = std::move(data);
			done = true;
		}, progress);

		while (!done)
		{
			http.PollRequests();
			if (!done)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		if (status == HTTPDownloader::HTTP_STATUS_CANCELLED)
		{
			Error::SetStringView(error, "Cancelled.");
			return false;
		}
		if (status != HTTPDownloader::HTTP_STATUS_OK)
		{
			Error::SetStringFmt(error, "Download failed (HTTP {}).", status);
			return false;
		}
		if (out->empty())
		{
			Error::SetStringView(error, "Download was empty.");
			return false;
		}
		return true;
	}

	void SetStatus(ProgressCallback* progress, const std::string& text)
	{
		if (progress)
			progress->SetStatusText(text.c_str());
	}

	bool IsCancelled(ProgressCallback* progress)
	{
		return progress && progress->IsCancelled();
	}
} // namespace

std::vector<ShaderPacks::InstallResult> ShaderPacks::Install(const std::string& shaders_root, std::span<const std::string> ids,
	ProgressCallback* progress)
{
	std::vector<InstallResult> results;
	const std::vector<std::string> order = ExpandDependencies(shaders_root, ids);
	if (order.empty())
		return results;

	std::unique_ptr<HTTPDownloader> http = HTTPDownloader::Create(Host::GetHTTPUserAgent());
	if (!http)
	{
		results.push_back({order.front(), false, false, "Failed to create HTTP downloader."});
		return results;
	}
	http->SetTimeout(DOWNLOAD_TIMEOUT_SECONDS);

	if (progress)
		progress->SetCancellable(true);

	std::vector<std::string> failed_ids;
	for (const std::string& id : order)
	{
		InstallResult& result = results.emplace_back();
		result.id = id;
		const PackInfo* pack = FindPack(id);
		if (!pack)
		{
			result.message = fmt::format("Unknown shader pack '{}'.", id);
			continue;
		}

		if (IsCancelled(progress))
		{
			result.cancelled = true;
			result.message = "Cancelled.";
			break;
		}

		if (pack->depends_on && std::find(failed_ids.begin(), failed_ids.end(), pack->depends_on) != failed_ids.end())
		{
			const PackInfo* dep = FindPack(pack->depends_on);
			result.message = fmt::format("Requires {}, which failed to install.", dep ? dep->display_name : pack->depends_on);
			failed_ids.push_back(id);
			continue;
		}

		Error error;

		// 1. Resolve.
		SetStatus(progress, fmt::format("Checking {}...", pack->display_name));
		const std::optional<ResolvedVersion> version = ResolveLatest(*pack, *http, &error);
		if (!version.has_value())
		{
			result.message = error.GetDescription();
			failed_ids.push_back(id);
			continue;
		}

		// 2. Download.
		SetStatus(progress, fmt::format("Downloading {}...", pack->display_name));
		std::vector<u8> archive;
		if (!DownloadToMemory(*http, version->download_url, progress, &archive, &error))
		{
			result.cancelled = (error.GetDescription() == "Cancelled.");
			result.message = error.GetDescription();
			if (result.cancelled)
				break;
			failed_ids.push_back(id);
			continue;
		}
		SetStatus(progress, fmt::format("Downloaded {} ({:.1f} MB).", pack->display_name, static_cast<double>(archive.size()) / 1048576.0));

		// 3. Validate.
		zip_error_t ze = {};
		auto zip = zip_open_buffer_managed(archive.data(), archive.size(), ZIP_RDONLY, 0, &ze);
		if (!zip)
		{
			result.message = fmt::format("Downloaded file for {} is not a valid zip archive.", pack->display_name);
			failed_ids.push_back(id);
			continue;
		}

		// 4. Remove the previous version, now that the replacement is known to be good.
		const std::string install_dir = Path::Combine(shaders_root, pack->install_subdir);
		if (const std::optional<InstalledPack> previous = ReadMarker(shaders_root, id); previous.has_value())
		{
			SetStatus(progress, fmt::format("Removing previous {}...", pack->display_name));
			for (const std::string& rel : previous->files)
			{
				const std::string path = Path::Combine(shaders_root, rel);
				if (FileSystem::FileExists(path.c_str()) && !FileSystem::DeleteFilePath(path.c_str()))
					WARNING_LOG("ShaderPacks: failed to delete '{}'.", path);
			}
			PruneEmptyDirectories(install_dir);
		}

		// 5. Extract.
		SetStatus(progress, fmt::format("Extracting {}...", pack->display_name));
		std::vector<std::string> written;
		const bool extracted = ShaderPackArchive::ExtractZipToDirectory(zip.get(), install_dir, pack->strip_components, progress, &written, &error);

		// 6. Record whatever landed, so a retry or uninstall can clean up.
		InstalledPack marker;
		marker.id = id;
		marker.version = version->version;
		marker.source_url = version->download_url;
		marker.installed_at = CurrentTimestamp();
		marker.files.reserve(written.size());
		for (const std::string& rel : written)
			marker.files.push_back(fmt::format("{}/{}", pack->install_subdir, rel));
		Error marker_error;
		if (!WriteMarker(shaders_root, marker, &marker_error))
			WARNING_LOG("ShaderPacks: {}", marker_error.GetDescription());

		if (!extracted)
		{
			result.cancelled = (error.GetDescription() == "Cancelled.");
			result.message = error.GetDescription();
			if (result.cancelled)
				break;
			failed_ids.push_back(id);
			continue;
		}

		result.success = true;
		INFO_LOG("ShaderPacks: installed {} {} ({} files).", pack->display_name, version->version, written.size());
	}

	return results;
}

std::vector<ShaderPacks::InstallResult> ShaderPacks::Install(std::span<const std::string> ids, ProgressCallback* progress)
{
	return Install(EmuFolders::Shaders, ids, progress);
}

bool ShaderPacks::Uninstall(std::string_view id, Error* error)
{
	return Uninstall(EmuFolders::Shaders, id, error);
}
