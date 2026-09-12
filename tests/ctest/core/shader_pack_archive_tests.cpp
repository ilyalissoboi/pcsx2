// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPackArchive.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ZipHelpers.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
					"pcsx2_shader_pack_archive_test_" + std::to_string(i));
				if (!FileSystem::DirectoryExists(candidate.c_str()) &&
					FileSystem::CreateDirectoryPath(candidate.c_str(), false))
					m_root = std::move(candidate);
			}
		}
		~TempRoot() { FileSystem::RecursiveDeleteDirectory(m_root.c_str()); }
		const std::string& root() const { return m_root; }

	private:
		std::string m_root;
	};

	// entries: (name, content); a name ending in '/' is added as a directory entry.
	std::string MakeZip(const std::string& dir, const std::vector<std::pair<std::string, std::string>>& entries)
	{
		const std::string path = Path::Combine(dir, "test.zip");
		int err = 0;
		zip_t* z = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
		if (!z)
			return {};
		for (const auto& [name, content] : entries)
		{
			if (name.ends_with('/'))
			{
				zip_dir_add(z, name.c_str(), ZIP_FL_ENC_UTF_8);
				continue;
			}
			zip_source_t* src = zip_source_buffer(z, content.data(), content.size(), 0);
			zip_file_add(z, name.c_str(), src, ZIP_FL_ENC_UTF_8);
		}
		if (zip_close(z) != 0)
			return {};
		return path;
	}

	std::string ReadAll(const std::string& path)
	{
		return FileSystem::ReadFileToString(path.c_str()).value_or("<missing>");
	}
} // namespace

TEST(ShaderPackArchive, TransformEntryNameRules)
{
	using ShaderPackArchive::EntryDisposition;
	std::string rel;
	Error error;

	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/crt/a.slangp", 1, &rel, &error), EntryDisposition::Extract);
	EXPECT_EQ(rel, "crt/a.slangp");
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("a/b/c/d/crt/x.slang", 4, &rel, &error), EntryDisposition::Extract);
	EXPECT_EQ(rel, "crt/x.slang");
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/", 1, &rel, &error), EntryDisposition::Skip); // directory
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/crt/", 1, &rel, &error), EntryDisposition::Skip);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("a/b/c/d", 4, &rel, &error), EntryDisposition::Skip); // strips to nothing
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/__MACOSX/crt/._a.slangp", 1, &rel, &error), EntryDisposition::Skip);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/crt/.DS_Store", 1, &rel, &error), EntryDisposition::Skip);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top/../evil.slang", 1, &rel, &error), EntryDisposition::Reject);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("/abs/evil.slang", 1, &rel, &error), EntryDisposition::Reject);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("top\\crt\\a.slangp", 1, &rel, &error), EntryDisposition::Reject);
	EXPECT_EQ(ShaderPackArchive::TransformEntryName("C:/evil.slang", 0, &rel, &error), EntryDisposition::Reject);
}

TEST(ShaderPackArchive, ExtractStripsSkipsAndRecordsFiles)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	const std::string zip_path = MakeZip(t.root(), {
		{"slang-shaders-abc/", ""},
		{"slang-shaders-abc/README.md", "readme"},
		{"slang-shaders-abc/crt/", ""},
		{"slang-shaders-abc/crt/crt-geom.slangp", "shader0 = x"},
		{"slang-shaders-abc/crt/shaders/geom.slang", "#version 450"},
		{"__MACOSX/slang-shaders-abc/crt/._crt-geom.slangp", "junk"},
		{"slang-shaders-abc/crt/.DS_Store", "junk"},
	});
	ASSERT_FALSE(zip_path.empty());

	zip_error_t ze = {};
	auto zip = zip_open_managed(zip_path.c_str(), ZIP_RDONLY, &ze);
	ASSERT_TRUE(zip);

	const std::string dest = Path::Combine(t.root(), "out");
	std::vector<std::string> written;
	Error error;
	ASSERT_TRUE(ShaderPackArchive::ExtractZipToDirectory(zip.get(), dest, 1, nullptr, &written, &error)) << error.GetDescription();

	ASSERT_EQ(written.size(), 3u);
	EXPECT_EQ(written[0], "README.md");
	EXPECT_EQ(written[1], "crt/crt-geom.slangp");
	EXPECT_EQ(written[2], "crt/shaders/geom.slang");
	EXPECT_EQ(ReadAll(Path::Combine(dest, "crt/shaders/geom.slang")), "#version 450");
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(dest, "crt/.DS_Store").c_str()));
	EXPECT_FALSE(FileSystem::DirectoryExists(Path::Combine(dest, "__MACOSX").c_str()));
}

TEST(ShaderPackArchive, ExtractAbortsOnTraversalAndKeepsWrittenList)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	const std::string zip_path = MakeZip(t.root(), {
		{"top/good1.slang", "1"},
		{"top/../escape.slang", "evil"},
		{"top/good2.slang", "2"},
	});
	ASSERT_FALSE(zip_path.empty());

	zip_error_t ze = {};
	auto zip = zip_open_managed(zip_path.c_str(), ZIP_RDONLY, &ze);
	ASSERT_TRUE(zip);

	const std::string dest = Path::Combine(t.root(), "out");
	std::vector<std::string> written;
	Error error;
	EXPECT_FALSE(ShaderPackArchive::ExtractZipToDirectory(zip.get(), dest, 1, nullptr, &written, &error));
	EXPECT_NE(error.GetDescription().find("escape.slang"), std::string::npos) << error.GetDescription();
	ASSERT_EQ(written.size(), 1u);
	EXPECT_EQ(written[0], "good1.slang");
	EXPECT_TRUE(FileSystem::FileExists(Path::Combine(dest, "good1.slang").c_str()));
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(dest, "good2.slang").c_str()));
	EXPECT_FALSE(FileSystem::FileExists(Path::Combine(t.root(), "escape.slang").c_str()));
}

TEST(ShaderPackArchive, ExtractWithZeroStripKeepsFullPaths)
{
	TempRoot t;
	ASSERT_FALSE(t.root().empty());
	const std::string zip_path = MakeZip(t.root(), {{"retro crisis/4K Flat/RC - PS2.slangp", "#reference x"}});
	ASSERT_FALSE(zip_path.empty());
	zip_error_t ze = {};
	auto zip = zip_open_managed(zip_path.c_str(), ZIP_RDONLY, &ze);
	ASSERT_TRUE(zip);

	const std::string dest = Path::Combine(t.root(), "out");
	std::vector<std::string> written;
	Error error;
	ASSERT_TRUE(ShaderPackArchive::ExtractZipToDirectory(zip.get(), dest, 0, nullptr, &written, &error)) << error.GetDescription();
	ASSERT_EQ(written.size(), 1u);
	EXPECT_EQ(written[0], "retro crisis/4K Flat/RC - PS2.slangp");
	EXPECT_EQ(ReadAll(Path::Combine(dest, "retro crisis/4K Flat/RC - PS2.slangp")), "#reference x");
}
