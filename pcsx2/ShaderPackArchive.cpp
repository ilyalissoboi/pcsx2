// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPackArchive.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/StringUtil.h"

#include "fmt/format.h"
#include "zip.h"

#include <memory>

ShaderPackArchive::EntryDisposition ShaderPackArchive::TransformEntryName(std::string_view entry_name, u32 strip_components,
	std::string* relative_out, Error* error)
{
	if (entry_name.empty())
		return EntryDisposition::Skip;

	// Reject anything that could escape the destination before looking at the components.
	if (entry_name.front() == '/' || entry_name.find('\\') != std::string_view::npos ||
		(entry_name.size() >= 2 && entry_name[1] == ':'))
	{
		Error::SetStringFmt(error, "Refusing unsafe archive entry '{}'.", entry_name);
		return EntryDisposition::Reject;
	}

	const bool is_directory = entry_name.back() == '/';
	std::vector<std::string_view> parts;
	size_t start = 0;
	while (start <= entry_name.size())
	{
		const size_t end = entry_name.find('/', start);
		const std::string_view part = entry_name.substr(start, (end == std::string_view::npos) ? std::string_view::npos : end - start);
		if (part == "..")
		{
			Error::SetStringFmt(error, "Refusing unsafe archive entry '{}'.", entry_name);
			return EntryDisposition::Reject;
		}
		if (!part.empty())
			parts.push_back(part);
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}

	if (is_directory || parts.size() <= strip_components)
		return EntryDisposition::Skip;

	for (const auto& part : parts)
	{
		if (part == "__MACOSX")
			return EntryDisposition::Skip;
	}
	if (parts.back() == ".DS_Store")
		return EntryDisposition::Skip;

	relative_out->clear();
	for (size_t i = strip_components; i < parts.size(); i++)
	{
		if (i > strip_components)
			relative_out->push_back('/');
		relative_out->append(parts[i]);
	}
	return EntryDisposition::Extract;
}

bool ShaderPackArchive::ExtractZipToDirectory(zip_t* zip, const std::string& dest_dir, u32 strip_components,
	ProgressCallback* progress, std::vector<std::string>* written, Error* error)
{
	const zip_int64_t num_entries = zip_get_num_entries(zip, 0);
	if (num_entries < 0)
	{
		Error::SetStringView(error, "Archive has no entries.");
		return false;
	}

	if (!FileSystem::CreateDirectoryPath(dest_dir.c_str(), true, error))
		return false;

	// Canonical destination prefix used to double-check every output path.
	// Prefix must use the native separator because Canonicalize does.
	std::string dest_prefix = Path::Canonicalize(dest_dir);
	if (!dest_prefix.empty() && dest_prefix.back() != '/' && dest_prefix.back() != '\\')
		dest_prefix.push_back(FS_OSPATH_SEPARATOR_CHARACTER);

	if (progress)
		progress->SetProgressRange(static_cast<u32>(num_entries));

	std::vector<u8> buffer;
	std::string relative;
	for (zip_int64_t i = 0; i < num_entries; i++)
	{
		if (progress && progress->IsCancelled())
		{
			Error::SetStringView(error, "Cancelled.");
			return false;
		}

		zip_stat_t st;
		if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &st) != 0 || !(st.valid & ZIP_STAT_NAME))
		{
			Error::SetStringFmt(error, "Failed to read archive entry {}.", i);
			return false;
		}

		switch (TransformEntryName(st.name, strip_components, &relative, error))
		{
			case EntryDisposition::Skip:
				if (progress)
					progress->SetProgressValue(static_cast<u32>(i + 1));
				continue;
			case EntryDisposition::Reject:
				return false;
			case EntryDisposition::Extract:
				break;
		}

		std::string out_path = Path::Combine(dest_dir, relative);
		const std::string canonical = Path::Canonicalize(out_path);
		if (!canonical.starts_with(dest_prefix))
		{
			Error::SetStringFmt(error, "Refusing to write '{}' outside the destination directory.", st.name);
			return false;
		}

		const std::string parent(Path::GetDirectory(out_path));
		if (!parent.empty() && !FileSystem::CreateDirectoryPath(parent.c_str(), true, error))
			return false;

		zip_file_t* zf = zip_fopen_index(zip, static_cast<zip_uint64_t>(i), 0);
		if (!zf)
		{
			Error::SetStringFmt(error, "Failed to open archive entry '{}'.", st.name);
			return false;
		}
		const size_t size = (st.valid & ZIP_STAT_SIZE) ? static_cast<size_t>(st.size) : 0;
		buffer.resize(size);
		size_t total = 0;
		while (total < size)
		{
			const zip_int64_t got = zip_fread(zf, buffer.data() + total, size - total);
			if (got <= 0)
				break;
			total += static_cast<size_t>(got);
		}
		zip_fclose(zf);
		if (total != size)
		{
			Error::SetStringFmt(error, "Failed to read archive entry '{}'.", st.name);
			return false;
		}

		if (!FileSystem::WriteBinaryFile(out_path.c_str(), buffer.data(), buffer.size()))
		{
			Error::SetStringFmt(error, "Failed to write '{}'.", out_path);
			return false;
		}

		written->push_back(relative);
		if (progress)
			progress->SetProgressValue(static_cast<u32>(i + 1));
	}

	return true;
}
