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
