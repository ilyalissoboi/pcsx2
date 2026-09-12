// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/LibrashaderLoader.h"
#include "Config.h"

#include "common/Console.h"
#include "common/DynamicLibrary.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#ifdef __APPLE__
#include "common/CocoaTools.h"
#endif

#include "fmt/format.h"

#include <mutex>

namespace
{
	struct LoadedLibrary
	{
		DynamicLibrary lib;
		ShaderChain::CommonFunctions fns;
		ShaderChain::Availability avail;
	};

	template <typename T>
	bool Resolve(DynamicLibrary& lib, const char* name, T* out, std::string* reason)
	{
		if (lib.GetSymbol(name, out))
			return true;
		*reason = fmt::format("librashader is missing symbol {}", name);
		return false;
	}

	bool LoadInto(LoadedLibrary& l, const std::string& path)
	{
		l.avail = {};
		if (path.empty())
		{
			l.avail.reason = "librashader path is empty";
			return false;
		}

		Error error;
		if (!l.lib.Open(path.c_str(), &error))
		{
			l.avail.reason = fmt::format("librashader could not be loaded from {}: {}", path, error.GetDescription());
			return false;
		}

		std::string& r = l.avail.reason;
		ShaderChain::CommonFunctions& f = l.fns;
		if (!Resolve(l.lib, "libra_instance_abi_version", &f.abi_version, &r) ||
			!Resolve(l.lib, "libra_instance_api_version", &f.api_version, &r))
		{
			l.lib.Close();
			return false;
		}

		const LIBRASHADER_ABI_VERSION abi = f.abi_version();
		if (abi != LIBRASHADER_CURRENT_ABI)
		{
			r = fmt::format("librashader ABI {} does not match the expected ABI {}", abi, LIBRASHADER_CURRENT_ABI);
			l.lib.Close();
			return false;
		}

		if (!Resolve(l.lib, "libra_error_errno", &f.error_errno, &r) ||
			!Resolve(l.lib, "libra_error_write", &f.error_write, &r) ||
			!Resolve(l.lib, "libra_error_free_string", &f.error_free_string, &r) ||
			!Resolve(l.lib, "libra_error_free", &f.error_free, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_create", &f.preset_ctx_create, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_free", &f.preset_ctx_free, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_set_core_name", &f.preset_ctx_set_core_name, &r) ||
			!Resolve(l.lib, "libra_preset_ctx_set_runtime", &f.preset_ctx_set_runtime, &r) ||
			!Resolve(l.lib, "libra_preset_create_with_options", &f.preset_create_with_options, &r) ||
			!Resolve(l.lib, "libra_preset_free", &f.preset_free, &r) ||
			!Resolve(l.lib, "libra_preset_get_runtime_params", &f.preset_get_runtime_params, &r) ||
			!Resolve(l.lib, "libra_preset_free_runtime_params", &f.preset_free_runtime_params, &r))
		{
			l.lib.Close();
			return false;
		}

		l.avail.available = true;
		l.avail.reason.clear();
		return true;
	}

	LoadedLibrary& Global()
	{
		static LoadedLibrary s_lib;
		return s_lib;
	}

	std::once_flag s_load_once;
} // namespace

std::string ShaderChain::GetDefaultLibraryPath()
{
#ifdef _WIN32
	return Path::Combine(EmuFolders::AppRoot, "librashader.dll");
#elif defined(__APPLE__)
	if (const std::optional<std::string> bundle = CocoaTools::GetBundlePath(); bundle.has_value())
	{
		std::string in_bundle = Path::Combine(*bundle, "Contents/Frameworks/librashader.dylib");
		if (FileSystem::FileExists(in_bundle.c_str()))
			return in_bundle;
	}
	return Path::Combine(EmuFolders::AppRoot, "librashader.dylib");
#else
	return Path::Combine(EmuFolders::AppRoot, "librashader.so");
#endif
}

ShaderChain::Availability ShaderChain::LoadFromPath(const std::string& path)
{
	LoadedLibrary local;
	LoadInto(local, path);
	return local.avail; // local.lib closes on scope exit
}

const ShaderChain::Availability& ShaderChain::GetAvailability()
{
	std::call_once(s_load_once, []() {
		LoadedLibrary& g = Global();
		const std::string path = GetDefaultLibraryPath();
		if (LoadInto(g, path))
			INFO_LOG("librashader loaded from {} (ABI {}, API {})", path, g.fns.abi_version(), g.fns.api_version());
		else
			WARNING_LOG("Shader chain unavailable: {}", g.avail.reason);
	});
	return Global().avail;
}

void* ShaderChain::GetSymbol(const char* name)
{
	if (!GetAvailability().available)
		return nullptr;
	return Global().lib.GetSymbolAddress(name);
}

const ShaderChain::CommonFunctions& ShaderChain::Common()
{
	GetAvailability();
	return Global().fns;
}

std::string ShaderChain::DescribeAndFreeError(libra_error_t error)
{
	if (!error)
		return "unknown librashader error";

	const CommonFunctions& f = Common();
	if (!f.error_write)
		return "librashader error (library not loaded)";

	std::string result;
	char* text = nullptr;
	if (f.error_write(error, &text) == 0 && text)
	{
		result = text;
		f.error_free_string(&text);
	}
	else
	{
		result = fmt::format("librashader error code {}", static_cast<int>(f.error_errno(error)));
	}
	f.error_free(&error);
	return result;
}
