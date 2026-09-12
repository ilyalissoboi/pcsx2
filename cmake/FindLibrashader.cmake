# - Try to find librashader (runtime-loaded shared library)
# Once done this will define
#  LIBRASHADER_FOUND - System has librashader
#  LIBRASHADER_LIBRARY - Path to librashader.dylib / librashader.dll

find_library(
    LIBRASHADER_LIBRARY
    NAMES librashader.dylib librashader.dll librashader
    PATHS ${ADDITIONAL_LIBRARY_PATHS}
    PATH_SUFFIXES lib bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Librashader DEFAULT_MSG LIBRASHADER_LIBRARY)

if(LIBRASHADER_FOUND)
    add_library(Librashader::librashader UNKNOWN IMPORTED)
    set_target_properties(Librashader::librashader PROPERTIES IMPORTED_LOCATION ${LIBRASHADER_LIBRARY})
endif()

mark_as_advanced(LIBRASHADER_LIBRARY)
