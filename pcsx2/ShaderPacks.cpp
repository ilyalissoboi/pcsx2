// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPacks.h"
#include "Config.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/HTTPDownloader.h"
#include "common/StringUtil.h"

#include "fmt/chrono.h"
#include "fmt/format.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <ctime>

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
			pack.files.emplace_back(v.GetString(), v.GetStringLength());
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
