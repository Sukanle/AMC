# SPDX-License-Identifier: Apache-2.0

# fmt and toml++ are supplied by the user, not bundled or downloaded by AMC.
# Install them with the system package manager (or build them yourself) and
# point CMAKE_PREFIX_PATH, fmt_DIR or tomlplusplus_DIR at the install prefix
# when they are not in a default location.
#
# Only minimum supported versions are enforced: any newer release, including a
# newer major version, is accepted and used as-is.

include_guard(GLOBAL)

set(AMC_FMT_MIN_VERSION
    "11.2.0"
    CACHE STRING "Minimum supported fmt version")
set(AMC_TOMLPLUSPLUS_MIN_VERSION
    "3.4.0"
    CACHE STRING "Minimum supported toml++ version")

find_package(fmt ${AMC_FMT_MIN_VERSION} CONFIG REQUIRED)
find_package(tomlplusplus ${AMC_TOMLPLUSPLUS_MIN_VERSION} CONFIG REQUIRED)

if(NOT TARGET fmt::fmt)
    message(FATAL_ERROR "fmt ${AMC_FMT_MIN_VERSION} or newer is required but no fmt::fmt target was created")
endif()

if(NOT TARGET tomlplusplus::tomlplusplus)
    message(
        FATAL_ERROR
            "toml++ ${AMC_TOMLPLUSPLUS_MIN_VERSION} or newer is required but no tomlplusplus::tomlplusplus target was created"
    )
endif()
