// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderChainParams.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>

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
					"pcsx2_shader_params_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempTree() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }
		void file(const char* rel, const char* contents = "#reference nothing\n")
		{
			const std::string full = Path::Combine(m_root, rel);
			FileSystem::CreateDirectoryPath(std::string(Path::GetDirectory(full)).c_str(), true);
			FileSystem::WriteStringToFile(full.c_str(), contents);
		}

	private:
		std::string m_root;
	};
} // namespace

TEST(ShaderChainParams, ParseOverridesRoundTripsThroughFormat)
{
	const ShaderChainParams::ParamList in = {{"GAMMA", 2.4f}, {"OFFSET", -0.25f}, {"BIG", 12345.5f}};
	const std::vector<std::string> entries = ShaderChainParams::FormatOverrides(in);
	ASSERT_EQ(entries.size(), 3u);
	EXPECT_EQ(entries[0], "GAMMA=2.4");
	EXPECT_EQ(entries[1], "OFFSET=-0.25");
	EXPECT_EQ(entries[2], "BIG=12345.5");

	const ShaderChainParams::ParamList out = ShaderChainParams::ParseOverrides(entries);
	ASSERT_EQ(out.size(), 3u);
	EXPECT_EQ(out[0].first, "GAMMA");
	EXPECT_FLOAT_EQ(out[0].second, 2.4f);
	EXPECT_EQ(out[1].first, "OFFSET");
	EXPECT_FLOAT_EQ(out[1].second, -0.25f);
	EXPECT_EQ(out[2].first, "BIG");
	EXPECT_FLOAT_EQ(out[2].second, 12345.5f);
}

TEST(ShaderChainParams, ParseOverridesSkipsMalformedEntries)
{
	const ShaderChainParams::ParamList out = ShaderChainParams::ParseOverrides({
		"no_equals_sign",
		"=1.0",
		"NOT_A_NUMBER=abc",
		"EMPTY_VALUE=",
		" SPACED = 3 ",
		"GOOD=1e-2",
	});
	ASSERT_EQ(out.size(), 2u);
	EXPECT_EQ(out[0].first, "SPACED");
	EXPECT_FLOAT_EQ(out[0].second, 3.0f);
	EXPECT_EQ(out[1].first, "GOOD");
	EXPECT_FLOAT_EQ(out[1].second, 0.01f);
}

TEST(ShaderChainParams, ParseOverridesLastDuplicateWins)
{
	const ShaderChainParams::ParamList out = ShaderChainParams::ParseOverrides({"A=1", "B=2", "A=3"});
	ASSERT_EQ(out.size(), 2u);
	EXPECT_EQ(out[0].first, "A");
	EXPECT_FLOAT_EQ(out[0].second, 3.0f);
	EXPECT_EQ(out[1].first, "B");
	EXPECT_FLOAT_EQ(out[1].second, 2.0f);
}

TEST(ShaderChainParams, FormatOverridesOfEmptyListIsEmpty)
{
	EXPECT_TRUE(ShaderChainParams::FormatOverrides({}).empty());
	EXPECT_STREQ(ShaderChainParams::SettingsSection(), "ShaderChainParams");
}

TEST(ShaderChainParams, NextFavoriteStepsAndWraps)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("a.slangp");
	t.file("sub/b.slangp");
	t.file("c.slangp");
	const std::vector<std::string> favs = {"a.slangp", "sub/b.slangp", "c.slangp"};

	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "a.slangp", true), "sub/b.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "sub/b.slangp", true), "c.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "c.slangp", true), "a.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "a.slangp", false), "c.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "c.slangp", false), "sub/b.slangp");
}

TEST(ShaderChainParams, NextFavoriteWhenCurrentNotListed)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("a.slangp");
	t.file("c.slangp");
	const std::vector<std::string> favs = {"a.slangp", "c.slangp"};

	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "", true), "a.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "other.slangp", true), "a.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "other.slangp", false), "c.slangp");
}

TEST(ShaderChainParams, NextFavoriteSkipsMissingFiles)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("a.slangp");
	t.file("c.slangp");
	const std::vector<std::string> favs = {"a.slangp", "missing.slangp", "c.slangp"};

	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "a.slangp", true), "c.slangp");
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), favs, "c.slangp", false), "a.slangp");
	// Only the current entry exists: stepping lands on it again rather than returning empty.
	EXPECT_EQ(ShaderChainParams::NextFavoriteIn(t.root(), {"a.slangp", "missing.slangp"}, "a.slangp", true), "a.slangp");
}

TEST(ShaderChainParams, NextFavoriteEmptyOrAllMissingReturnsEmpty)
{
	TempTree t;
	ASSERT_FALSE(t.root().empty());
	EXPECT_TRUE(ShaderChainParams::NextFavoriteIn(t.root(), {}, "a.slangp", true).empty());
	EXPECT_TRUE(ShaderChainParams::NextFavoriteIn(t.root(), {"x.slangp", "y.slangp"}, "", true).empty());
	EXPECT_TRUE(ShaderChainParams::NextFavoriteIn(t.root(), {"../escape.slangp"}, "", true).empty());
}
