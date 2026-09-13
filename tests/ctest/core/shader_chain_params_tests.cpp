// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/ShaderChainParams.h"
#include "GS/ShaderChain/LibrashaderLoader.h"
#include "Host.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
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

	/// Installs a settings layer for the duration of a test. The game layer is used because the base
	/// layer may only be set once per process (Host::Internal::SetBaseSettingsLayer asserts on reset),
	/// and it is consulted before the base layer by LayeredSettingsInterface.
	class ScopedSettingsLayer
	{
	public:
		explicit ScopedSettingsLayer(SettingsInterface* sif) { Install(sif); }
		~ScopedSettingsLayer() { Install(nullptr); }

	private:
		static void Install(SettingsInterface* sif)
		{
			auto lock = Host::GetSettingsLock();
			Host::Internal::SetGameSettingsLayer(sif, lock);
		}
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

TEST(ShaderChainParams, DecimalsForStepFollowsStepMagnitude)
{
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(1.0f), 0);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.5f), 1);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.05f), 2);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.01f), 2);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.001f), 3);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.0001f), 4);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.000001f), 4);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(16.0f), 0);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(100.0f), 0);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(0.0f), 3);
	EXPECT_EQ(ShaderChainParams::DecimalsForStep(-1.0f), 3);
}

TEST(ShaderChainParams, IsDefaultValueUsesTightRelativeTolerance)
{
	EXPECT_TRUE(ShaderChainParams::IsDefaultValue(1.0f, 1.0f));
	EXPECT_TRUE(ShaderChainParams::IsDefaultValue(0.1f * 3.0f, 0.3f));       // slider arithmetic noise
	EXPECT_FALSE(ShaderChainParams::IsDefaultValue(1.02f, 1.0f));            // typed value near default, step 0.05
	EXPECT_FALSE(ShaderChainParams::IsDefaultValue(1008.0f, 1000.0f));       // coarse step 16
	EXPECT_TRUE(ShaderChainParams::IsDefaultValue(1000.0f + 0.0001f, 1000.0f)); // within float noise at 1000
	EXPECT_FALSE(ShaderChainParams::IsDefaultValue(0.0f, 0.001f));
}

TEST(ShaderChainParams, ApplyOverridesToStorePushesParsedListForPreset)
{
	MemorySettingsInterface layer;
	layer.SetStringList("ShaderChainParams", "shaders_slang/crt/x.slangp", {"GAMMA=2.4", "bad", "SIZE=3"});
	const ScopedSettingsLayer scoped_layer(&layer);

	ShaderChainParams::ApplyOverridesToStore("shaders_slang/crt/x.slangp");
	std::string preset;
	ShaderChainParams::ParamList params;
	const u64 gen1 = ShaderPresets::Params().Snapshot(&preset, &params);
	EXPECT_EQ(preset, "shaders_slang/crt/x.slangp");
	ASSERT_EQ(params.size(), 2u);
	EXPECT_EQ(params[0].first, "GAMMA");
	EXPECT_FLOAT_EQ(params[0].second, 2.4f);
	EXPECT_EQ(params[1].first, "SIZE");

	ShaderChainParams::ApplyOverridesToStore("other.slangp"); // no key -> empty list, generation bumps
	const u64 gen2 = ShaderPresets::Params().Snapshot(&preset, &params);
	EXPECT_GT(gen2, gen1);
	EXPECT_EQ(preset, "other.slangp");
	EXPECT_TRUE(params.empty());
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

namespace
{
	// Minimal preset + shader pair. Enumeration only preprocesses the shader (no compilation), but
	// keep the GLSL valid so the fixture also loads in a real chain.
	constexpr const char* FIXTURE_SLANGP = "shaders = 1\nshader0 = params.slang\n";
	constexpr const char* FIXTURE_SLANG =
		"#version 450\n"
		"#pragma parameter TEST_GAIN \"Test gain\" 1.0 0.0 2.0 0.1\n"
		"#pragma parameter TEST_FLAG \"Test flag\" 0.0 0.0 1.0 1.0\n"
		"layout(push_constant) uniform Push { float TEST_GAIN; float TEST_FLAG; } params;\n"
		"layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;\n"
		"#pragma stage vertex\n"
		"layout(location = 0) in vec4 Position;\n"
		"layout(location = 1) in vec2 TexCoord;\n"
		"layout(location = 0) out vec2 vTexCoord;\n"
		"void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }\n"
		"#pragma stage fragment\n"
		"layout(location = 0) in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 FragColor;\n"
		"layout(set = 0, binding = 2) uniform sampler2D Source;\n"
		"void main() { FragColor = texture(Source, vTexCoord) * params.TEST_GAIN; }\n";
} // namespace

TEST(ShaderChainParams, EnumerateParametersReadsPragmaParameters)
{
	if (!ShaderChain::GetAvailability().available)
		GTEST_SKIP() << "librashader not loadable: " << ShaderChain::GetAvailability().reason
					 << " (set PCSX2_LIBRASHADER_PATH to run this test)";

	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("fixture.slangp", FIXTURE_SLANGP);
	t.file("params.slang", FIXTURE_SLANG);

	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	ASSERT_TRUE(ShaderChainParams::EnumerateParameters(Path::Combine(t.root(), "fixture.slangp"), &params, &error))
		<< error.GetDescription();
	ASSERT_EQ(params.size(), 2u);
	EXPECT_EQ(params[0].name, "TEST_GAIN");
	EXPECT_EQ(params[0].description, "Test gain");
	EXPECT_FLOAT_EQ(params[0].initial, 1.0f);
	EXPECT_FLOAT_EQ(params[0].minimum, 0.0f);
	EXPECT_FLOAT_EQ(params[0].maximum, 2.0f);
	EXPECT_FLOAT_EQ(params[0].step, 0.1f);
	EXPECT_EQ(params[1].name, "TEST_FLAG");
	EXPECT_FLOAT_EQ(params[1].step, 1.0f);
}

TEST(ShaderChainParams, EnumerateParametersFailsForMissingShader)
{
	if (!ShaderChain::GetAvailability().available)
		GTEST_SKIP() << "librashader not loadable";

	TempTree t;
	ASSERT_FALSE(t.root().empty());
	t.file("broken.slangp", "shaders = 1\nshader0 = does_not_exist.slang\n");

	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	EXPECT_FALSE(ShaderChainParams::EnumerateParameters(Path::Combine(t.root(), "broken.slangp"), &params, &error));
	EXPECT_FALSE(error.GetDescription().empty());
	EXPECT_TRUE(params.empty());
}

TEST(ShaderChainParams, EnumerateParametersFailsForMissingPreset)
{
	if (!ShaderChain::GetAvailability().available)
		GTEST_SKIP() << "librashader not loadable";

	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	EXPECT_FALSE(ShaderChainParams::EnumerateParameters("/definitely/not/here.slangp", &params, &error));
	EXPECT_FALSE(error.GetDescription().empty());
}

TEST(ShaderChainParams, PersistActivePresetWritesBaseWhenGameLayerHasNoPreset)
{
	MemorySettingsInterface base, game;
	base.SetStringValue("EmuCore/GS", "ShaderChainPreset", "a.slangp");
	game.SetBoolValue("EmuCore/GS", "ShaderChainEnabled", false); // unrelated per-game key must not attract the write

	SettingsInterface* const target = ShaderChainParams::PersistActivePresetIn(&base, &game, "b.slangp");
	EXPECT_EQ(target, &base);
	EXPECT_EQ(base.GetStringValue("EmuCore/GS", "ShaderChainPreset"), "b.slangp");
	EXPECT_TRUE(base.GetBoolValue("EmuCore/GS", "ShaderChainEnabled", false));
	EXPECT_FALSE(game.ContainsValue("EmuCore/GS", "ShaderChainPreset"));
	EXPECT_FALSE(game.GetBoolValue("EmuCore/GS", "ShaderChainEnabled", true));
}

TEST(ShaderChainParams, PersistActivePresetWritesGameLayerWhenItOverridesPreset)
{
	MemorySettingsInterface base, game;
	base.SetStringValue("EmuCore/GS", "ShaderChainPreset", "a.slangp");
	game.SetStringValue("EmuCore/GS", "ShaderChainPreset", "g.slangp");

	SettingsInterface* const target = ShaderChainParams::PersistActivePresetIn(&base, &game, "b.slangp");
	EXPECT_EQ(target, &game);
	EXPECT_EQ(game.GetStringValue("EmuCore/GS", "ShaderChainPreset"), "b.slangp");
	EXPECT_TRUE(game.GetBoolValue("EmuCore/GS", "ShaderChainEnabled", false));
	EXPECT_EQ(base.GetStringValue("EmuCore/GS", "ShaderChainPreset"), "a.slangp");
	EXPECT_FALSE(base.ContainsValue("EmuCore/GS", "ShaderChainEnabled"));
}

TEST(ShaderChainParams, PersistActivePresetHandlesMissingLayers)
{
	MemorySettingsInterface base;
	EXPECT_EQ(ShaderChainParams::PersistActivePresetIn(&base, nullptr, "b.slangp"), &base);
	EXPECT_EQ(base.GetStringValue("EmuCore/GS", "ShaderChainPreset"), "b.slangp");
	EXPECT_EQ(ShaderChainParams::PersistActivePresetIn(nullptr, nullptr, "b.slangp"), nullptr);
}
