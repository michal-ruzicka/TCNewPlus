cmake_minimum_required(VERSION 3.20)
project(TCNewPlus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared settings for every target in this project.
add_compile_definitions(
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Source files are UTF-8 (no BOM). Without /utf-8 MSVC interprets non-ASCII
# bytes in the source using the system ANSI code page, which produces
# mojibake in L"..." wide string literals on machines whose ACP isn't UTF-8.
# /utf-8 is equivalent to /source-charset:utf-8 /execution-charset:utf-8.
if(MSVC)
    add_compile_options(/utf-8)

    # Reproducible builds: strip per-build noise from object files and zero the
    # PE TimeDateStamp so the same source always produces the same binary.
    # /Brepro removes the non-deterministic hash MSVC writes into every .obj.
    # /BREPRO (linker) zeroes the PE timestamp field.
    add_compile_options(/Brepro /experimental:deterministic)
    add_link_options(/BREPRO)

    # Normalize absolute source-file paths in debug info so binaries are
    # identical regardless of where the repo is checked out.
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/" _src_native)
    add_compile_options("/pathmap:${_src_native}=./")
endif()

# Link the static C/C++ runtime so the artifacts have no vcruntime*.dll
# dependency. Total Commander users expect drop-in install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# ---------------------------------------------------------------------------
# Version: version= in pluginst.inf is the single source of truth. It is
# turned into version.h (for the VERSIONINFO resource) at configure time, and
# CMake re-runs whenever pluginst.inf changes.
# ---------------------------------------------------------------------------

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS pluginst.inf)
file(STRINGS pluginst.inf _version_line REGEX "^version=")
# A pre-release suffix (X.Y.Z-devel between releases) is kept in the product
# version string; the numeric FILEVERSION carries MAJOR.MINOR.PATCH.0 only.
if(NOT _version_line MATCHES "^version=(([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.]+)?)[\r]*$")
    message(FATAL_ERROR "pluginst.inf: no version=MAJOR.MINOR.PATCH[-suffix] line")
endif()
set(TCNP_VERSION "${CMAKE_MATCH_1}")
set(TCNP_VER_MAJOR ${CMAKE_MATCH_2})
set(TCNP_VER_MINOR ${CMAKE_MATCH_3})
set(TCNP_VER_PATCH ${CMAKE_MATCH_4})

# ---------------------------------------------------------------------------
# Plugin DLL: TCNewPlus.wfx (32-bit) or TCNewPlus.wfx64 (64-bit)
# ---------------------------------------------------------------------------

add_library(TCNewPlus SHARED
    TCNewPlus.cpp
    NameSubstitution.cpp
    TCNewPlus.def
    TCNewPlus.rc   # Network Neighborhood plugin icon + VERSIONINFO
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TCNP_SUFFIX ".wfx64")
else()
    set(TCNP_SUFFIX ".wfx")
endif()
set_target_properties(TCNewPlus PROPERTIES
    SUFFIX "${TCNP_SUFFIX}"
    PREFIX "")

set(TCNP_FILE_NAME "TCNewPlus${TCNP_SUFFIX}")
configure_file(version.h.in version.h @ONLY)
target_include_directories(TCNewPlus PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ---------------------------------------------------------------------------
# Test executable: unit tests for the naming rules plus an end-to-end smoke
# test that loads the built plugin DLL through the WFX API. Run by build.cmd
# and the CI workflow; not part of the distributable ZIP.
# ---------------------------------------------------------------------------

add_executable(TCNewPlusTest
    TCNewPlusTest.cpp
    NameSubstitution.cpp
    app.manifest
)
target_include_directories(TCNewPlusTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
