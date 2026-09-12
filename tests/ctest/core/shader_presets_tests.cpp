// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderPresets.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <filesystem>

namespace
{
	class TempTree
	{
	public:
		TempTree()
		{
			for (int i = 0; i < 1000 && m_root.empty(); i++)
			{
				std::string candidate = Path::Combine(std::filesystem::temp_directory_path().string(),
					"pcsx2_shader_presets_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempTree() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }
		void file(const char* rel)
		{
			const std::string full = Path::Combine(m_root, rel);
			FileSystem::CreateDirectoryPath(std::string(Path::GetDirectory(full)).c_str(), true);
			FileSystem::WriteStringToFile(full.c_str(), "#reference nothing\n");
		}

	private:
		std::string m_root;
	};
} // namespace

TEST(ShaderPresets, EnumerateFindsNestedSlangpSortedWithForwardSlashes)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("shaders_slang/crt/crt-royale.slangp");
	t.file("shaders_slang/crt/shaders/royale/first.slang"); // not a preset
	t.file("shaders_slang/anti-aliasing/aa.slangp");
	t.file("top.slangp");

	const std::vector<std::string> result = ShaderPresets::EnumerateIn(t.root());
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "shaders_slang/anti-aliasing/aa.slangp");
	EXPECT_EQ(result[1], "shaders_slang/crt/crt-royale.slangp");
	EXPECT_EQ(result[2], "top.slangp");
}

TEST(ShaderPresets, EnumerateSkipsMacJunkAndDotFiles)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("shaders_slang/crt/good.slangp");
	t.file("shaders_slang/__MACOSX/crt/._good.slangp");
	t.file("shaders_slang/crt/.hidden.slangp");

	const std::vector<std::string> result = ShaderPresets::EnumerateIn(t.root());
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0], "shaders_slang/crt/good.slangp");
}

TEST(ShaderPresets, EnumerateOnMissingRootIsEmpty)
{
	EXPECT_TRUE(ShaderPresets::EnumerateIn("/definitely/not/here/pcsx2").empty());
}

TEST(ShaderPresets, ResolveRejectsEmptyAbsoluteAndEscaping)
{
	const std::string root = "/root/shaders";
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "/etc/passwd").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "../x.slangp").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "a/../../x.slangp").empty());
	EXPECT_TRUE(ShaderPresets::ResolvePresetPathIn(root, "C:/x.slangp").empty());
}

TEST(ShaderPresets, ResolveJoinsRelativeUnderRoot)
{
	const std::string root = "/root/shaders";
	const std::string got = ShaderPresets::ResolvePresetPathIn(root, "shaders_slang/crt/a.slangp");
	EXPECT_EQ(got, Path::Combine(root, "shaders_slang/crt/a.slangp"));
}

TEST(ShaderPresets, ParameterStoreGenerationAndSnapshot)
{
	ShaderPresets::ParameterStore store;
	const u64 g0 = store.GetGeneration();

	std::string preset;
	std::vector<std::pair<std::string, float>> params;
	EXPECT_EQ(store.Snapshot(&preset, &params), g0);
	EXPECT_TRUE(preset.empty());
	EXPECT_TRUE(params.empty());

	store.Set("shaders_slang/crt/a.slangp", {{"gamma", 2.4f}, {"mask", 1.0f}});
	EXPECT_GT(store.GetGeneration(), g0);
	const u64 g1 = store.Snapshot(&preset, &params);
	EXPECT_EQ(g1, store.GetGeneration());
	EXPECT_EQ(preset, "shaders_slang/crt/a.slangp");
	ASSERT_EQ(params.size(), 2u);
	EXPECT_EQ(params[0].first, "gamma");
	EXPECT_FLOAT_EQ(params[0].second, 2.4f);
	EXPECT_EQ(params[1].first, "mask");
	EXPECT_FLOAT_EQ(params[1].second, 1.0f);

	store.Set("shaders_slang/crt/a.slangp", {});
	EXPECT_GT(store.GetGeneration(), g1);
}
