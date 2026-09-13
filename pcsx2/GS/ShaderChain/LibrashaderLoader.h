// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Runtime-independent part of the librashader C API. Backends define their own
// LIBRA_RUNTIME_* macro and include librashader.h themselves for the chain functions.
#include "librashader.h"

#include <string>

namespace ShaderChain
{
	struct Availability
	{
		bool available = false;
		std::string reason;
	};

	struct CommonFunctions
	{
		PFN_libra_instance_abi_version abi_version = nullptr;
		PFN_libra_instance_api_version api_version = nullptr;
		PFN_libra_error_errno error_errno = nullptr;
		PFN_libra_error_write error_write = nullptr;
		PFN_libra_error_free_string error_free_string = nullptr;
		PFN_libra_error_free error_free = nullptr;
		PFN_libra_preset_ctx_create preset_ctx_create = nullptr;
		PFN_libra_preset_ctx_free preset_ctx_free = nullptr;
		PFN_libra_preset_ctx_set_core_name preset_ctx_set_core_name = nullptr;
		PFN_libra_preset_ctx_set_runtime preset_ctx_set_runtime = nullptr;
		PFN_libra_preset_create_with_options preset_create_with_options = nullptr;
		PFN_libra_preset_free preset_free = nullptr;
		PFN_libra_preset_get_runtime_params preset_get_runtime_params = nullptr;
		PFN_libra_preset_free_runtime_params preset_free_runtime_params = nullptr;
	};

	/// Loads the library on first call (thread-safe) and caches the result.
	const Availability& GetAvailability();

	/// Raw symbol lookup for backend-specific entry points. Returns nullptr if unavailable.
	void* GetSymbol(const char* name);

	/// Runtime-independent entry points. Only valid when GetAvailability().available.
	const CommonFunctions& Common();

	/// Converts a libra_error_t into a human readable string and frees it. Safe on null.
	std::string DescribeAndFreeError(libra_error_t error);

	/// Platform default location: next to the executable (Windows) or Contents/Frameworks (macOS bundle).
	/// The PCSX2_LIBRASHADER_PATH environment variable overrides both.
	std::string GetDefaultLibraryPath();

	/// Attempts to load from an explicit path without affecting global state. Used by tests.
	Availability LoadFromPath(const std::string& path);
} // namespace ShaderChain
