# SPDX-License-Identifier: Apache-2.0

# Pinned third-party dependencies for AMC.
#
# The Linux distribution packages are not API-compatible across releases (for
# example, Ubuntu 24.04 ships fmt 9, which lacks fmt::println). AMC therefore
# requires a minimum version and downloads the pinned tarball into
# ./dependencies when a suitable package is not installed.
#
# The downloaded sources are intentionally untracked; only
# dependencies/README.md is tracked.

include_guard(GLOBAL)
include(FetchContent)

set(AMC_FMT_VERSION
    "11.2.0"
    CACHE STRING "fmt version used when no suitable fmt package is found")
set(AMC_TOMLPLUSPLUS_VERSION
    "3.4.0"
    CACHE STRING "toml++ version used when no suitable tomlplusplus package is found")

if(POLICY CMP0135)
    # Extract URL archives with their timestamps rather than the current time.
    cmake_policy(SET CMP0135 NEW)
endif()

find_package(fmt ${AMC_FMT_VERSION} CONFIG QUIET)
set(AMC_FMT_FETCHED OFF)
if(NOT fmt_FOUND)
    set(AMC_FMT_FETCHED ON)
    message(STATUS "fmt ${AMC_FMT_VERSION} not found; fetching it into ${AMC_DEPENDENCIES_DIR}/fmt")

    # Keep the fetched dependency a build-tree-only library.
    set(FMT_TEST OFF)
    set(FMT_DOC OFF)
    set(FMT_INSTALL OFF)
    FetchContent_Declare(
        fmt
        URL https://github.com/fmtlib/fmt/archive/refs/tags/${AMC_FMT_VERSION}.tar.gz
        SOURCE_DIR ${AMC_DEPENDENCIES_DIR}/fmt)
    FetchContent_MakeAvailable(fmt)

    # AMC links fmt into the LLDB shared module; a non-PIC static fmt would
    # fail at link time on ELF platforms.
    if(TARGET fmt)
        set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

if(NOT TARGET fmt::fmt)
    message(FATAL_ERROR "fmt ${AMC_FMT_VERSION} is required but no fmt::fmt target was created")
endif()

find_package(tomlplusplus ${AMC_TOMLPLUSPLUS_VERSION} CONFIG QUIET)
set(AMC_TOMLPLUSPLUS_FETCHED OFF)
if(NOT tomlplusplus_FOUND)
    set(AMC_TOMLPLUSPLUS_FETCHED ON)
    message(
        STATUS
            "toml++ ${AMC_TOMLPLUSPLUS_VERSION} not found; fetching it into ${AMC_DEPENDENCIES_DIR}/tomlplusplus")

    set(TOMLPLUSPLUS_BUILD_TESTS OFF)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF)
    set(TOMLPLUSPLUS_INSTALL OFF)
    FetchContent_Declare(
        tomlplusplus
        URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v${AMC_TOMLPLUSPLUS_VERSION}.tar.gz
        SOURCE_DIR ${AMC_DEPENDENCIES_DIR}/tomlplusplus)
    FetchContent_MakeAvailable(tomlplusplus)
endif()

if(NOT TARGET tomlplusplus::tomlplusplus)
    message(
        FATAL_ERROR
            "toml++ ${AMC_TOMLPLUSPLUS_VERSION} is required but no tomlplusplus::tomlplusplus target was created")
endif()
