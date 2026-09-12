// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/ShaderChain/LibrashaderLoader.h"
#include "common/Path.h"
#include <gtest/gtest.h>

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
	const std::string path = ShaderChain::GetDefaultLibraryPath();
#ifdef _WIN32
	EXPECT_TRUE(path.ends_with("librashader.dll")) << path;
#elif defined(__APPLE__)
	EXPECT_TRUE(path.ends_with("librashader.dylib")) << path;
#else
	EXPECT_TRUE(path.ends_with("librashader.so")) << path;
#endif
}

TEST(LibrashaderLoader, DescribeNullErrorDoesNotCrash)
{
	EXPECT_EQ(ShaderChain::DescribeAndFreeError(nullptr), "unknown librashader error");
}
