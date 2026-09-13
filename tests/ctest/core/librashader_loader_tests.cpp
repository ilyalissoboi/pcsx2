// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/LibrashaderLoader.h"
#include <cstdlib>
#include "common/Path.h"
#include <gtest/gtest.h>

namespace
{
	void SetEnvVar(const char* name, const char* value)
	{
#ifdef _WIN32
		_putenv_s(name, value);
#else
		if (value[0] == '\0')
			unsetenv(name);
		else
			setenv(name, value, 1);
#endif
	}
} // namespace

TEST(LibrashaderLoader, MissingLibraryIsUnavailableWithReason)
{
	const std::string path = "/definitely/not/here/librashader.dylib";
	const ShaderChain::Availability avail = ShaderChain::LoadFromPath(path);
	EXPECT_FALSE(avail.available);
	EXPECT_NE(avail.reason.find("librashader"), std::string::npos);
	EXPECT_NE(avail.reason.find(path), std::string::npos);
}

TEST(LibrashaderLoader, WrongLibraryIsUnavailableBecauseSymbolsAreMissing)
{
#ifdef __APPLE__
	const std::string path = "/usr/lib/libSystem.B.dylib";
#elif defined(_WIN32)
	const std::string path = "C:\\Windows\\System32\\kernel32.dll";
#else
	const std::string path = "libc.so.6";
#endif
	const ShaderChain::Availability avail = ShaderChain::LoadFromPath(path);
	EXPECT_FALSE(avail.available);
	EXPECT_NE(avail.reason.find("libra_instance_abi_version"), std::string::npos);
}

TEST(LibrashaderLoader, DefaultPathHasPlatformFileName)
{
	// The environment override would replace the platform default, so drop it for this test.
	const char* previous = std::getenv("PCSX2_LIBRASHADER_PATH");
	const std::string saved = previous ? previous : "";
	SetEnvVar("PCSX2_LIBRASHADER_PATH", "");

	const std::string path = ShaderChain::GetDefaultLibraryPath();
#ifdef _WIN32
	EXPECT_TRUE(path.ends_with("librashader.dll")) << path;
#elif defined(__APPLE__)
	EXPECT_TRUE(path.ends_with("librashader.dylib")) << path;
#else
	EXPECT_TRUE(path.ends_with("librashader.so")) << path;
#endif

	SetEnvVar("PCSX2_LIBRASHADER_PATH", saved.c_str());
}

TEST(LibrashaderLoader, DescribeNullErrorDoesNotCrash)
{
	EXPECT_EQ(ShaderChain::DescribeAndFreeError(nullptr), "unknown librashader error");
}

TEST(LibrashaderLoader, EnvironmentOverrideWinsOverDefaultPath)
{
	const char* previous = std::getenv("PCSX2_LIBRASHADER_PATH");
	const std::string saved = previous ? previous : "";

	SetEnvVar("PCSX2_LIBRASHADER_PATH", "/tmp/override/librashader-test.dylib");
	EXPECT_EQ(ShaderChain::GetDefaultLibraryPath(), "/tmp/override/librashader-test.dylib");

	SetEnvVar("PCSX2_LIBRASHADER_PATH", saved.c_str());
	if (saved.empty())
		EXPECT_NE(ShaderChain::GetDefaultLibraryPath(), "/tmp/override/librashader-test.dylib");
}
