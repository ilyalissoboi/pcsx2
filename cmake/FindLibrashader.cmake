# - Try to find librashader (runtime-loaded shared library)
# Once done this will define
#  LIBRASHADER_FOUND - System has librashader
#  LIBRASHADER_LIBRARY - Path to librashader.dylib / librashader.dll

if(WIN32)
    # The DLL is loaded at runtime and ships without an import library, so find_library (which
    # looks for .lib files on Windows) cannot locate it. Look for the DLL itself in <prefix>/bin.
    find_file(
        LIBRASHADER_LIBRARY
        NAMES librashader.dll
        PATHS ${CMAKE_PREFIX_PATH} ${ADDITIONAL_LIBRARY_PATHS}
        PATH_SUFFIXES bin
    )
else()
    find_library(
        LIBRASHADER_LIBRARY
        NAMES librashader.dylib librashader
        PATHS ${ADDITIONAL_LIBRARY_PATHS}
        PATH_SUFFIXES lib
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Librashader DEFAULT_MSG LIBRASHADER_LIBRARY)

if(LIBRASHADER_FOUND)
    add_library(Librashader::librashader UNKNOWN IMPORTED)
    set_target_properties(Librashader::librashader PROPERTIES IMPORTED_LOCATION ${LIBRASHADER_LIBRARY})
endif()

mark_as_advanced(LIBRASHADER_LIBRARY)
