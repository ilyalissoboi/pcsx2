// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class Error;
class ProgressCallback;
struct zip;
typedef struct zip zip_t;

/// Extraction of downloaded shader pack archives into the Shaders folder.
namespace ShaderPackArchive
{
	/// Extraction is aborted when a single entry, or the sum of all extracted entries, exceeds these
	/// caps; an archive that claims a huge entry size must not be able to exhaust memory.
	constexpr size_t MAX_ENTRY_SIZE = 64u * 1024 * 1024;
	constexpr size_t MAX_TOTAL_SIZE = 1024u * 1024 * 1024;

	enum class EntryDisposition
	{
		Extract, ///< write the file at *relative_out
		Skip, ///< directory entry, junk (__MACOSX, .DS_Store), or nothing left after stripping
		Reject, ///< unsafe name; the whole extraction must abort
	};

	/// Applies strip_components and the skip/reject rules to a zip entry name.
	EntryDisposition TransformEntryName(std::string_view entry_name, u32 strip_components, std::string* relative_out, Error* error);

	/// Extracts all entries of zip into dest_dir. Stops at the first rejected entry, oversized entry,
	/// write failure or cancellation and returns false; written always lists the files that were
	/// written, in order, relative to dest_dir with '/' separators. progress may be null.
	bool ExtractZipToDirectory(zip_t* zip, const std::string& dest_dir, u32 strip_components, ProgressCallback* progress,
		std::vector<std::string>* written, Error* error);

	/// ExtractZipToDirectory with caller-supplied size caps; exposed so tests can exercise the caps
	/// without building a multi-gigabyte archive.
	bool ExtractZipToDirectoryWithLimits(zip_t* zip, const std::string& dest_dir, u32 strip_components,
		ProgressCallback* progress, std::vector<std::string>* written, Error* error, size_t max_entry, size_t max_total);
} // namespace ShaderPackArchive
