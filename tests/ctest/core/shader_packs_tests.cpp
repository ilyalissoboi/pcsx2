// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPacks.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace
{
	class TempRoot
	{
	public:
		TempRoot()
		{
			for (int i = 0; i < 1000 && m_root.empty(); i++)
			{
				std::string candidate = Path::Combine(std::filesystem::temp_directory_path().string(),
					"pcsx2_shader_packs_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempRoot() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }
		void file(const char* rel, const char* contents = "x")
		{
			const std::string full = Path::Combine(m_root, rel);
			FileSystem::CreateDirectoryPath(std::string(Path::GetDirectory(full)).c_str(), true);
			FileSystem::WriteStringToFile(full.c_str(), contents);
		}

	private:
		std::string m_root;
	};
} // namespace

TEST(ShaderPacks, RegistryHasThreePacksWithSpecLayout)
{
	const auto packs = ShaderPacks::GetPacks();
	ASSERT_EQ(packs.size(), 3u);

	const ShaderPacks::PackInfo* slang = ShaderPacks::FindPack("shaders_slang");
	ASSERT_NE(slang, nullptr);
	EXPECT_STREQ(slang->github_repo, "libretro/slang-shaders");
	EXPECT_EQ(slang->version_source, ShaderPacks::VersionSource::BranchHead);
	EXPECT_STREQ(slang->branch, "master");
	EXPECT_EQ(slang->strip_components, 1u);
	EXPECT_STREQ(slang->install_subdir, "shaders_slang");
	EXPECT_EQ(slang->depends_on, nullptr);

	const ShaderPacks::PackInfo* rc = ShaderPacks::FindPack("retro-crisis-gdv-ntsc");
	ASSERT_NE(rc, nullptr);
	EXPECT_STREQ(rc->github_repo, "RetroCrisis/Retro-Crisis-GDV-NTSC");
	EXPECT_EQ(rc->version_source, ShaderPacks::VersionSource::LatestRelease);
	EXPECT_EQ(rc->strip_components, 1u);
	EXPECT_STREQ(rc->install_subdir, "shaders_slang/retro crisis");
	EXPECT_STREQ(rc->depends_on, "shaders_slang");

	const ShaderPacks::PackInfo* sat = ShaderPacks::FindPack("satpixie-crt");
	ASSERT_NE(sat, nullptr);
	EXPECT_STREQ(sat->github_repo, "Conkwer/satpixie-crt-shader");
	EXPECT_STREQ(sat->asset_exclude, "variants");
	EXPECT_EQ(sat->strip_components, 4u);
	EXPECT_STREQ(sat->install_subdir, "shaders_slang");
	EXPECT_EQ(sat->depends_on, nullptr);

	EXPECT_EQ(ShaderPacks::FindPack("nope"), nullptr);
}

TEST(ShaderPacks, MarkerRoundTrip)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "satpixie-crt").has_value());

	ShaderPacks::InstalledPack pack;
	pack.id = "satpixie-crt";
	pack.version = "20260122";
	pack.source_url = "https://example.invalid/a \"quoted\" name.zip";
	pack.installed_at = "2026-09-13T00:00:00Z";
	pack.files = {"shaders_slang/crt/satpixie-crt.slangp", "shaders_slang/crt/shaders/satpixie/blur_horiz.slang"};

	Error error;
	ASSERT_TRUE(ShaderPacks::WriteMarker(t.root(), pack, &error)) << error.GetDescription();
	EXPECT_TRUE(FileSystem::FileExists(ShaderPacks::GetMarkerPath(t.root(), "satpixie-crt").c_str()));

	const std::optional<ShaderPacks::InstalledPack> back = ShaderPacks::ReadMarker(t.root(), "satpixie-crt");
	ASSERT_TRUE(back.has_value());
	EXPECT_EQ(back->id, pack.id);
	EXPECT_EQ(back->version, pack.version);
	EXPECT_EQ(back->source_url, pack.source_url);
	EXPECT_EQ(back->installed_at, pack.installed_at);
	EXPECT_EQ(back->files, pack.files);

	EXPECT_TRUE(ShaderPacks::RemoveMarker(t.root(), "satpixie-crt"));
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "satpixie-crt").has_value());
}

TEST(ShaderPacks, MalformedMarkerReadsAsNotInstalled)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	t.file(".shaderpacks/shaders_slang.json", "{ not json");
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "shaders_slang").has_value());
	t.file(".shaderpacks/shaders_slang.json", "{\"id\":\"shaders_slang\"}"); // missing version/files
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "shaders_slang").has_value());
}

TEST(ShaderPacks, TimestampIsUtcIso8601)
{
	const std::string ts = ShaderPacks::CurrentTimestamp();
	ASSERT_EQ(ts.size(), 20u) << ts;
	EXPECT_EQ(ts[4], '-');
	EXPECT_EQ(ts[10], 'T');
	EXPECT_EQ(ts[19], 'Z');
}
TEST(ShaderPacks, ParseCommitJsonYieldsSha)
{
	std::string sha;
	Error error;
	ASSERT_TRUE(ShaderPacks::ParseCommitJson(R"({"sha":"0123456789abcdef0123456789abcdef01234567","commit":{}})", &sha, &error)) << error.GetDescription();
	EXPECT_EQ(sha, "0123456789abcdef0123456789abcdef01234567");

	EXPECT_FALSE(ShaderPacks::ParseCommitJson("{ nope", &sha, &error));
	EXPECT_FALSE(error.GetDescription().empty());
	EXPECT_FALSE(ShaderPacks::ParseCommitJson(R"({"commit":{}})", &sha, &error));
}

TEST(ShaderPacks, ParseReleaseJsonPicksMatchingZipAsset)
{
	const char* json = R"({
		"tag_name": "20260122",
		"assets": [
			{"name": "satpixie-crt-shader-variants-20260122.zip", "browser_download_url": "https://x/variants.zip"},
			{"name": "README.md", "browser_download_url": "https://x/readme"},
			{"name": "satpixie-crt-shader-20260122.zip", "browser_download_url": "https://x/main.zip"}
		]})";
	ShaderPacks::ResolvedVersion out;
	Error error;
	ASSERT_TRUE(ShaderPacks::ParseReleaseJson(json, "variants", &out, &error)) << error.GetDescription();
	EXPECT_EQ(out.version, "20260122");
	EXPECT_EQ(out.download_url, "https://x/main.zip");

	// Without an exclusion the first .zip wins.
	ASSERT_TRUE(ShaderPacks::ParseReleaseJson(json, nullptr, &out, &error));
	EXPECT_EQ(out.download_url, "https://x/variants.zip");

	// RetroCrisis publishes a doubled extension; it still ends in .zip.
	const char* rc = R"({"tag_name":"20260820","assets":[{"name":"Retro.Crisis.GDV-NTSC.2026.08.20.zip.zip","browser_download_url":"https://x/rc.zip.zip"}]})";
	ASSERT_TRUE(ShaderPacks::ParseReleaseJson(rc, nullptr, &out, &error));
	EXPECT_EQ(out.version, "20260820");
	EXPECT_EQ(out.download_url, "https://x/rc.zip.zip");
}

TEST(ShaderPacks, ParseReleaseJsonErrors)
{
	ShaderPacks::ResolvedVersion out;
	Error error;
	EXPECT_FALSE(ShaderPacks::ParseReleaseJson("not json", nullptr, &out, &error));
	EXPECT_FALSE(ShaderPacks::ParseReleaseJson(R"({"tag_name":"v1","assets":[{"name":"a.tar.gz","browser_download_url":"u"}]})", nullptr, &out, &error));
	EXPECT_NE(error.GetDescription().find("zip"), std::string::npos);
	EXPECT_FALSE(ShaderPacks::ParseReleaseJson(R"({"assets":[{"name":"a.zip","browser_download_url":"u"}]})", nullptr, &out, &error)); // no tag_name
}

TEST(ShaderPacks, VersionUrlsFollowGitHubApi)
{
	EXPECT_EQ(ShaderPacks::GetVersionUrl(*ShaderPacks::FindPack("shaders_slang")), "https://api.github.com/repos/libretro/slang-shaders/commits/master");
	EXPECT_EQ(ShaderPacks::GetVersionUrl(*ShaderPacks::FindPack("satpixie-crt")), "https://api.github.com/repos/Conkwer/satpixie-crt-shader/releases/latest");
}

namespace
{
	ShaderPacks::InstalledPack MakeInstalled(const char* id, std::vector<std::string> files)
	{
		ShaderPacks::InstalledPack p;
		p.id = id;
		p.version = "v";
		p.installed_at = "2026-09-13T00:00:00Z";
		p.files = std::move(files);
		return p;
	}
} // namespace

TEST(ShaderPacks, ExpandDependenciesOrdersAndDedupes)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	Error error;

	const std::string rc = "retro-crisis-gdv-ntsc", slang = "shaders_slang", sat = "satpixie-crt";

	std::vector<std::string> ids{rc};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{slang, rc}));

	ids = {rc, slang};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{slang, rc}));

	ids = {sat};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{sat}));

	ids = {"bogus", sat};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{sat}));

	ASSERT_TRUE(ShaderPacks::WriteMarker(t.root(), MakeInstalled("shaders_slang", {"shaders_slang/stock.slang"}), &error));
	ids = {rc};
	EXPECT_EQ(ShaderPacks::ExpandDependencies(t.root(), ids), (std::vector<std::string>{rc}));
}

TEST(ShaderPacks, UninstallDeletesListedFilesOnly)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	Error error;

	t.file("shaders_slang/crt/satpixie-crt.slangp");
	t.file("shaders_slang/crt/shaders/satpixie/accumulate.slang");
	t.file("shaders_slang/crt/shaders/satpixie/blur_horiz.slang");
	t.file("shaders_slang/crt/crt-geom.slangp"); // belongs to another pack, must survive
	ASSERT_TRUE(ShaderPacks::WriteMarker(t.root(), MakeInstalled("satpixie-crt", {
		"shaders_slang/crt/satpixie-crt.slangp",
		"shaders_slang/crt/shaders/satpixie/accumulate.slang",
		"shaders_slang/crt/shaders/satpixie/blur_horiz.slang",
		"shaders_slang/crt/shaders/satpixie/already-gone.slang", // missing on disk: tolerated
	}), &error));

	ASSERT_TRUE(ShaderPacks::Uninstall(t.root(), "satpixie-crt", &error)) << error.GetDescription();
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(t.root(), "shaders_slang/crt/satpixie-crt.slangp").c_str()));
	EXPECT_FALSE(FileSystem::DirectoryExists(Path::Combine(t.root(), "shaders_slang/crt/shaders/satpixie").c_str())); // pruned
	EXPECT_FALSE(FileSystem::DirectoryExists(Path::Combine(t.root(), "shaders_slang/crt/shaders").c_str())); // pruned
	EXPECT_TRUE(FileSystem::FileExists(Path::Combine(t.root(), "shaders_slang/crt/crt-geom.slangp").c_str()));
	EXPECT_TRUE(FileSystem::DirectoryExists(Path::Combine(t.root(), "shaders_slang/crt").c_str())); // not empty
	EXPECT_FALSE(ShaderPacks::ReadMarker(t.root(), "satpixie-crt").has_value());

	EXPECT_FALSE(ShaderPacks::Uninstall(t.root(), "satpixie-crt", &error)); // not installed
	EXPECT_NE(error.GetDescription().find("not installed"), std::string::npos);
}

TEST(ShaderPacks, MarkerDropsUnsafeFilePaths)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	t.file(".shaderpacks/satpixie-crt.json",
		R"({"id":"satpixie-crt","version":"v","files":["shaders_slang/crt/ok.slangp","../../etc/passwd","/abs.slang","C:/win.slang","a\\b.slang","shaders_slang/../x.slang",""]})");
	const std::optional<ShaderPacks::InstalledPack> pack = ShaderPacks::ReadMarker(t.root(), "satpixie-crt");
	ASSERT_TRUE(pack.has_value());
	ASSERT_EQ(pack->files.size(), 1u);
	EXPECT_EQ(pack->files[0], "shaders_slang/crt/ok.slangp");
}
