#-------------------------------------------------------------------------------
#                       Search all libraries on the system
#-------------------------------------------------------------------------------
find_package(Git)

# Require threads on all OSes.
find_package(Threads REQUIRED)

# Dependency libraries.
# On macOS, Mono.framework contains an ancient version of libpng.  We don't want that.
# Avoid it by telling cmake to avoid finding frameworks while we search for libpng.
set(FIND_FRAMEWORK_BACKUP ${CMAKE_FIND_FRAMEWORK})
set(CMAKE_FIND_FRAMEWORK NEVER)
find_package(PNG 1.6.40 REQUIRED)
find_package(JPEG REQUIRED) # No version because flatpak uses libjpeg-turbo.
find_package(ZLIB REQUIRED) # v1.3, but Mac uses the SDK version.
find_package(Zstd 1.5.5 REQUIRED)
find_package(LZ4 REQUIRED)
find_package(WebP REQUIRED) # v1.3.2, spews an error on Linux because no pkg-config.
find_package(SDL3 3.2.6 REQUIRED)
find_package(Freetype 2.10 REQUIRED) # 2.10 is the first with COLRv0 support, which we need for rendering emoji
find_package(plutovg 1.1.0 REQUIRED)
find_package(plutosvg 0.0.7 REQUIRED)
find_package(ryml REQUIRED)
find_package(FFMPEG 7.1 COMPONENTS avcodec avformat avutil swresample swscale REQUIRED)
if (WIN32)
	find_package(DirectX-Headers 1.618.1 REQUIRED)
endif()

if(USE_VULKAN)
	find_package(Shaderc REQUIRED)
endif()

if(USE_LIBRASHADER)
	find_package(Librashader)
	if(NOT LIBRASHADER_FOUND)
		message(WARNING "USE_LIBRASHADER is ON but librashader was not found in CMAKE_PREFIX_PATH; the shader chain will report unavailable at runtime.")
	endif()
endif()

# Memory card sync (pcsx2/SIO/Memcard/MemcardSync.h). emusync is a Rust library: a pinned commit is
# fetched and built by cargo through emusync's own CMake module, which defines emusync::emusync and
# sets EMUSYNC_FOUND. Optional: without cargo, rustc or the Rust target (or with USE_EMUSYNC off) the
# feature compiles out. -DEMUSYNC_SOURCE_DIR=<checkout> builds from a local emusync checkout.
# MSBuild builds take emusync from the deps package instead (common/vsprops/common.props).
if(USE_EMUSYNC)
	# A plain variable, not a cache entry: a cached pin keeps its first value in an existing build
	# folder, so a bump would be silently ignored there.
	set(EMUSYNC_PIN "760171a04a2ccc9a762279c534b083327d6e8fdb")
	# emusync's module caches EMUSYNC_SOURCE_DIR. When that is FetchContent's own folder for emusync,
	# it is an earlier fetch, not a checkout given with -D: forget it, or a bump would keep the old
	# source.
	if(DEFINED CACHE{EMUSYNC_SOURCE_DIR})
		set(_emusync_fetch_base "${FETCHCONTENT_BASE_DIR}")
		if(NOT _emusync_fetch_base)
			set(_emusync_fetch_base "${CMAKE_BINARY_DIR}/_deps")
		endif()
		get_filename_component(_emusync_fetched "${_emusync_fetch_base}/emusync-src" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
		get_filename_component(_emusync_cached "$CACHE{EMUSYNC_SOURCE_DIR}" ABSOLUTE)
		if(_emusync_cached STREQUAL _emusync_fetched)
			unset(EMUSYNC_SOURCE_DIR CACHE)
		endif()
	endif()
	find_program(EMUSYNC_CARGO cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")
	if(NOT EMUSYNC_CARGO)
		message(STATUS "emusync: cargo not found -> memory card sync DISABLED. Install Rust (https://rustup.rs) to enable it.")
	else()
		if(NOT EMUSYNC_SOURCE_DIR)
			include(FetchContent)
			FetchContent_Declare(emusync
				GIT_REPOSITORY https://github.com/ilyalissoboi/emusync.git
				GIT_TAG ${EMUSYNC_PIN}
			)
			# emusync has no top-level CMakeLists.txt, so this only downloads it.
			FetchContent_MakeAvailable(emusync)
			set(EMUSYNC_SOURCE_DIR "${emusync_SOURCE_DIR}")
		endif()
		include("${EMUSYNC_SOURCE_DIR}/bindings/cmake/EmuSync.cmake")
	endif()
endif()

# Platform-specific dependencies.
if (WIN32)
	add_subdirectory(3rdparty/D3D12MemAlloc EXCLUDE_FROM_ALL)
	add_subdirectory(3rdparty/winpixeventruntime EXCLUDE_FROM_ALL)
	add_subdirectory(3rdparty/winwil EXCLUDE_FROM_ALL)
	find_package(Vtune)
else()
	find_package(CURL REQUIRED)
	find_package(PCAP REQUIRED)
	find_package(Vtune)

	## Use CheckLib package to find module
	include(CheckLib)

	if(UNIX AND NOT APPLE)
		find_package(Fontconfig REQUIRED)
		if(LINUX)
			check_lib(LIBUDEV libudev libudev.h)
		endif()

		if(X11_API)
			find_package(X11 REQUIRED)
			if (NOT X11_Xrandr_FOUND)
				message(FATAL_ERROR "XRandR extension is required")
			endif()
		endif()

		if(WAYLAND_API)
			find_package(ECM REQUIRED NO_MODULE)
			list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
			find_package(Wayland REQUIRED Egl)
		endif()

		if(USE_BACKTRACE)
			find_package(Libbacktrace REQUIRED)
		endif()

		find_package(PkgConfig REQUIRED)
		pkg_check_modules(DBUS REQUIRED dbus-1)
	endif()
endif()

set(CMAKE_FIND_FRAMEWORK ${FIND_FRAMEWORK_BACKUP})

add_subdirectory(3rdparty/fast_float EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/lzma EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/libchdr EXCLUDE_FROM_ALL)
disable_compiler_warnings_for_target(libchdr)
add_subdirectory(3rdparty/soundtouch EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/simpleini EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/imgui EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/cpuinfo EXCLUDE_FROM_ALL)
disable_compiler_warnings_for_target(cpuinfo)
add_subdirectory(3rdparty/libzip EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/rcheevos EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/rapidjson EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/discord-rpc EXCLUDE_FROM_ALL)
add_subdirectory(3rdparty/freesurround EXCLUDE_FROM_ALL)

if(USE_OPENGL)
	add_subdirectory(3rdparty/glad EXCLUDE_FROM_ALL)
endif()

if(USE_VULKAN)
	add_subdirectory(3rdparty/vulkan EXCLUDE_FROM_ALL)
endif()

add_subdirectory(3rdparty/cubeb EXCLUDE_FROM_ALL)
disable_compiler_warnings_for_target(cubeb)
disable_compiler_warnings_for_target(speex)

# Find the Qt components that we need.
if(ENABLE_QT_UI)
	find_package(Qt6 6.10 COMPONENTS CoreTools Core GuiTools Gui WidgetsTools Widgets LinguistTools REQUIRED)

	if(NOT WIN32 AND NOT APPLE)
		if (Qt6_VERSION VERSION_GREATER_EQUAL 6.10.0)
			find_package(Qt6 COMPONENTS CorePrivate GuiPrivate WidgetsPrivate REQUIRED)
		endif()
	endif()

	# The docking system for the debugger.
	find_package(KDDockWidgets-qt6 2.3.0 REQUIRED)
endif()

if(WIN32 AND ARCH_X86)
	add_subdirectory(3rdparty/rainterface EXCLUDE_FROM_ALL)
endif()

# Demangler for the debugger.
add_subdirectory(3rdparty/demangler EXCLUDE_FROM_ALL)

# Symbol table parser.
add_subdirectory(3rdparty/ccc EXCLUDE_FROM_ALL)

# Architecture-specific.
if(ARCH_X86)
	add_subdirectory(3rdparty/zydis EXCLUDE_FROM_ALL)
elseif(ARCH_ARM64)
	add_subdirectory(3rdparty/vixl EXCLUDE_FROM_ALL)
endif()

# Prevent fmt from being built with exceptions, or being thrown at call sites.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFMT_USE_EXCEPTIONS=0 -DFMT_USE_RTTI=0")
add_subdirectory(3rdparty/fmt EXCLUDE_FROM_ALL)

# Deliberately at the end. We don't want to set the flag on third-party projects.
if(MSVC)
	# Don't warn about "deprecated" POSIX functions.
	add_definitions("-D_CRT_NONSTDC_NO_WARNINGS" "-D_CRT_SECURE_NO_WARNINGS" "-DCRT_SECURE_NO_DEPRECATE")
endif()
