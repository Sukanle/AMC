# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(
    AMC
    VERSION 0.3.1
    LANGUAGES C CXX)

# Standalone AMC build. This repository prefers an installed ABIX package and
# does not assume that the ABIX source tree is its parent.
#
#   cmake -S . -B build \
#     -DABIX_DIR=<prefix>/lib/cmake/ABIX \
#     -DCMAKE_PREFIX_PATH=<prefix>;/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus
#
# Only ABIX is fetched automatically: when no ABIX 0.3 package is found, the
# build clones ABIX with its submodules into ./dependencies and configures it
# as a runtime-only subproject. LLVM/Clang, fmt and toml++ are expected to be
# installed by the user; AMC uses the versions already present on the system.
#
# The ABIX runtime remains header-only. AMC owns the artifact libraries
# (format/abi/metadata/tools) and keeps its own exception/RTTI policy.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)
if(POLICY CMP0097)
    # An empty GIT_SUBMODULES below means "none": ABIX's submodules are
    # initialized explicitly after the checkout so their URLs can be rewritten.
    cmake_policy(SET CMP0097 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CodeIn templates are plain source assets (Rust metadata reader, future
# backends). The generated Config.h exposes the build-tree path; installed
# providers resolve the install-relative path at runtime.
set(AMC_CODEIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CodeIn")
set(AMC_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AMC_GENERATED_INCLUDE_DIR}/SKL/AMC")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.h.in" "${AMC_GENERATED_INCLUDE_DIR}/SKL/AMC/Config.h" @ONLY)

option(AMC_BUILD_TESTS "Build AMC unit/e2e tests" ON)
option(AMC_BUILD_BENCHMARKS "Build Google Benchmark performance baselines" ON)
option(AMC_INSTALL "Generate AMC install rules" ON)
option(AMC_INSTALL_PRIVATE_HEADERS "Install AMC private implementation headers" OFF)
option(AMC_ENABLE_LLDB_PLUGIN "Build the optional LLDB plugin" ON)
option(AMC_BUILD_AUE "Build the optional Aue Lua conformance prototype" ON)
set(AMC_ABIX_REF
    "v0.3.0"
    CACHE STRING "ABIX git revision used when FetchContent downloads ABIX (tag, branch or commit)")
set(AMC_DEPENDENCIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies")

# Prefer an installed ABIX package. If it is missing, fetch ABIX with its
# submodules into a repository-local checkout that is intentionally untracked.
find_package(ABIX 0.3 CONFIG QUIET COMPONENTS runtime)
set(AMC_ABIX_FETCHED OFF)
if(NOT ABIX_FOUND)
    set(AMC_ABIX_FETCHED ON)

    find_package(Git QUIET)
    if(NOT GIT_EXECUTABLE)
        message(FATAL_ERROR "git is required to fetch ABIX")
    endif()

    # ABIX is consumed as a runtime-only subproject: no tests, benchmarks,
    # DLL variants or install rules are generated from the fetched checkout.
    # Its bundled MICS is added by ABIX itself, so keep MICS build-tree-only.
    set(ABIX_BUILD_RUNTIME_ONLY ON)
    set(ABIX_INSTALL OFF)
    set(MICS_INSTALL OFF)
    set(MICS_BUILD_TESTS OFF)

    # A checkout already created by a previous configure is reused directly:
    # FetchContent would otherwise still run its update step and print a
    # "fetching" status on every configure. Delete dependencies/ABIX or pass a
    # new AMC_ABIX_REF/clean build directory when a real refresh is needed.
    if(EXISTS "${AMC_DEPENDENCIES_DIR}/ABIX/CMakeLists.txt")
        message(
            STATUS
                "ABIX 0.3 package not found; using existing checkout at ${AMC_DEPENDENCIES_DIR}/ABIX (delete it to refetch)"
        )
        set(abix_SOURCE_DIR "${AMC_DEPENDENCIES_DIR}/ABIX")
        set(abix_BINARY_DIR "${CMAKE_BINARY_DIR}/abix")
    else()
        message(STATUS "ABIX 0.3 package not found; fetching ABIX into ${AMC_DEPENDENCIES_DIR}")

        # Populate ABIX without its submodules and without adding it yet: its
        # .gitmodules uses SSH URLs, which fail on machines without GitHub SSH
        # credentials. The rewritten submodules are initialized below, then
        # ABIX is added as a subproject.
        FetchContent_Declare(
            ABIX
            GIT_REPOSITORY https://github.com/Sukanle/ABIX.git
            GIT_TAG ${AMC_ABIX_REF}
            GIT_SUBMODULES ""
            GIT_PROGRESS TRUE
            SOURCE_DIR ${AMC_DEPENDENCIES_DIR}/ABIX SOURCE_SUBDIR amc-deferred-subproject)
        FetchContent_MakeAvailable(ABIX)
    endif()

    # Rewrite every SSH submodule URL in .gitmodules to HTTPS in the local
    # repository config before initializing the submodules.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} config -f .gitmodules --get-regexp "^submodule\\..*\\.url$"
        WORKING_DIRECTORY "${abix_SOURCE_DIR}"
        OUTPUT_VARIABLE AMC_ABIX_SUBMODULE_URLS
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" AMC_ABIX_SUBMODULE_URLS "${AMC_ABIX_SUBMODULE_URLS}")
    foreach(AMC_ABIX_SUBMODULE_URL IN LISTS AMC_ABIX_SUBMODULE_URLS)
        if(AMC_ABIX_SUBMODULE_URL MATCHES "^submodule\\.([^.]+)\\.url git@github\\.com:(.+)$")
            execute_process(COMMAND ${GIT_EXECUTABLE} config "submodule.${CMAKE_MATCH_1}.url"
                                    "https://github.com/${CMAKE_MATCH_2}" WORKING_DIRECTORY "${abix_SOURCE_DIR}")
        endif()
    endforeach()
    unset(AMC_ABIX_SUBMODULE_URLS)

    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY "${abix_SOURCE_DIR}"
        RESULT_VARIABLE AMC_ABIX_SUBMODULE_RESULT)
    if(NOT AMC_ABIX_SUBMODULE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to initialize the ABIX submodules")
    endif()

    add_subdirectory("${abix_SOURCE_DIR}" "${abix_BINARY_DIR}")

    if(NOT TARGET ABIX::runtime)
        message(FATAL_ERROR "Fetched ABIX does not provide the ABIX::runtime target")
    endif()
    set(ABIX_INCLUDE_DIR "${abix_SOURCE_DIR}/include")
    set(ABIX_INCLUDE_DIRS "${ABIX_INCLUDE_DIR}" "${abix_SOURCE_DIR}/MICS/include" "${abix_SOURCE_DIR}/MICS/HLMD")
endif()

# The tests launch compilers directly, so expose the include roots as a native
# path list (os.pathsep in the Python harness).
if(WIN32)
    # CTest's ENVIRONMENT property is itself a semicolon-separated list, so the
    # Windows path separators must be escaped to keep the value intact.
    string(REPLACE ";" "\\;" AMC_ABIX_INCLUDE_DIRS_ENV "${ABIX_INCLUDE_DIRS}")
else()
    string(JOIN ":" AMC_ABIX_INCLUDE_DIRS_ENV ${ABIX_INCLUDE_DIRS})
endif()

# EPCD owns the stable application-layer error identity. AMC requires Core
# (ErrorCode / static domains / conditions); Interface is used for registration
# when the installed or fetched EPCD provides it.
set(AMC_EPCD_REF
    "main"
    CACHE STRING "EPCD git revision used when FetchContent downloads EPCD")
# Older AMC revisions pinned a local-only tag that was never pushed to the
# public EPCD repository. Migrate that cache entry in place so an existing
# build directory does not keep failing on the missing ref.
if(AMC_EPCD_REF STREQUAL "v0.1.0-baseline")
    message(STATUS "AMC_EPCD_REF legacy tag v0.1.0-baseline -> main (EPCD has no such remote tag)")
    set(AMC_EPCD_REF
        "main"
        CACHE STRING "EPCD git revision used when FetchContent downloads EPCD" FORCE)
endif()
find_package(EPCD 0.1 CONFIG QUIET COMPONENTS Core)
if(NOT EPCD_FOUND)
    find_package(Git QUIET)
    if(NOT GIT_EXECUTABLE)
        message(FATAL_ERROR "git is required to fetch EPCD")
    endif()
    if(EXISTS "${AMC_DEPENDENCIES_DIR}/EPCD/CMakeLists.txt")
        message(
            STATUS
                "EPCD 0.1 package not found; using existing checkout at ${AMC_DEPENDENCIES_DIR}/EPCD (delete it to refetch)"
        )
        set(epcd_SOURCE_DIR "${AMC_DEPENDENCIES_DIR}/EPCD")
        set(epcd_BINARY_DIR "${CMAKE_BINARY_DIR}/epcd")
        add_subdirectory("${epcd_SOURCE_DIR}" "${epcd_BINARY_DIR}")
    else()
        message(STATUS "EPCD 0.1 package not found; fetching EPCD into ${AMC_DEPENDENCIES_DIR}")
        set(EPCD_BUILD_TESTS
            OFF
            CACHE BOOL "Do not build EPCD tests from the AMC checkout" FORCE)
        set(EPCD_INSTALL
            OFF
            CACHE BOOL "Do not install fetched EPCD" FORCE)
        FetchContent_Declare(
            epcd
            GIT_REPOSITORY https://github.com/Sukanle/EPCD.git
            GIT_TAG ${AMC_EPCD_REF}
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
            SOURCE_DIR ${AMC_DEPENDENCIES_DIR}/EPCD)
        FetchContent_MakeAvailable(epcd)
    endif()
endif()
if(NOT TARGET EPCD::Core)
    message(FATAL_ERROR "EPCD::Core is unavailable; install EPCD or check the EPCD checkout")
endif()
set(AMC_EPCD_TARGETS EPCD::Core)
if(TARGET EPCD::Interface)
    list(APPEND AMC_EPCD_TARGETS EPCD::Interface)
    set(AMC_HAS_EPCD_INTERFACE ON)
else()
    set(AMC_HAS_EPCD_INTERFACE OFF)
endif()

find_package(LLVM CONFIG REQUIRED)
llvm_map_components_to_libnames(AMC_LLVM_SUPPORT_LIBS support)
# LLVM's cxxflags include -D_GNU_SOURCE, but GCC/Clang on Linux already define
# it for C++, which produces a redefinition warning on every provider TU.
separate_arguments(AMC_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(REMOVE_ITEM AMC_LLVM_DEFINITIONS "-D_GNU_SOURCE")
endif()

# Prefer the LLVM shared library so providers do not each embed a copy of
# LLVMSupport. Fall back to component static libraries when the installed LLVM
# has no dylib target (notably MSVC builds, where LLVM_BUILD_LLVM_DYLIB is
# unsupported).
option(AMC_LLVM_DYNAMIC "Link AMC providers against the shared LLVM library when available" ON)
set(AMC_LLVM_LINK_LIBRARIES ${AMC_LLVM_SUPPORT_LIBS})
if(AMC_LLVM_DYNAMIC
   AND TARGET LLVM
   AND LLVM_LINK_LLVM_DYLIB)
    set(AMC_LLVM_LINK_LIBRARIES LLVM)
    message(STATUS "LLVM providers: dynamic LLVM (${LLVM_PACKAGE_VERSION})")
else()
    message(STATUS "LLVM providers: static component libraries (${LLVM_PACKAGE_VERSION})")
endif()

# Unified LLVM/Clang interface. Any target may opt in with
# `target_link_libraries(<target> ... amc-llvm)`; the target carries include
# paths, LLVM compile definitions and the shared/static LLVM link choice.
add_library(amc-llvm INTERFACE)
add_library(AMC::llvm ALIAS amc-llvm)
# The include paths come from the LLVM package found by this build. They are
# kept in BUILD_INTERFACE so the installed package resolves LLVM from the
# consumer's own installation (see AMCConfig.cmake.in) instead of embedding
# this machine's prefix.
target_include_directories(
    amc-llvm SYSTEM INTERFACE $<BUILD_INTERFACE:${LLVM_INCLUDE_DIRS}> $<BUILD_INTERFACE:${CLANG_INCLUDE_DIRS}>)
target_compile_options(amc-llvm INTERFACE ${AMC_LLVM_DEFINITIONS})
target_link_libraries(amc-llvm INTERFACE ${AMC_LLVM_LINK_LIBRARIES})
set_target_properties(amc-llvm PROPERTIES EXPORT_NAME llvm)

# Use Clang through clang-cpp instead of find_package(Clang). Some packagers
# (notably MSYS2) export Clang target files that reference static archives they
# do not ship, so configuring fails even though clang-cpp is installed. Clang
# headers live under the same include root as LLVM's.
find_library(
    AMC_CLANG_CPP_LIBRARY
    NAMES clang-cpp
    HINTS ${LLVM_LIBRARY_DIRS})
if(NOT AMC_CLANG_CPP_LIBRARY)
    message(
        FATAL_ERROR
            "clang-cpp not found; install the Clang development libraries next to LLVM (LLVM_LIBRARY_DIRS=${LLVM_LIBRARY_DIRS})"
    )
endif()
if(NOT CLANG_INCLUDE_DIRS)
    set(CLANG_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS})
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

set(AMC_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(AMC_INCLUDE_DIR ${AMC_SOURCE_DIR}/include)
set(SKL_ABIX_INC_DIR ${ABIX_INCLUDE_DIR})

find_program(
    AMC_CLANG_EXECUTABLE
    NAMES clang
    HINTS ${LLVM_TOOLS_BINARY_DIR})
if(AMC_CLANG_EXECUTABLE)
    execute_process(
        COMMAND ${AMC_CLANG_EXECUTABLE} -print-resource-dir
        OUTPUT_VARIABLE AMC_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT AMC_CLANG_RESOURCE_DIR)
    message(WARNING "Could not detect the Clang resource directory; amc-cpp may not find built-in headers")
endif()

# AMC owns the artifact-layer libraries (`format`, `abi`, `metadata`, `tools`).
# They are compiled in this project; ABIX supplies only the runtime headers and
# MICS. This keeps the AMC release independent without splitting the AMC core
# headers/namespaces across two repositories.
add_library(
    libabix-format STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Core.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Diagnostics/Error.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Diagnostics/Diagnostics.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Diagnostics/Verify.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Error.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/IR/Condition.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Semantic/Identity.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Semantic/Serialize.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Semantic/Sync.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/IR/LoweringJson.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/IR/Artifact.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/IR/TypeMapping.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Support/Json.cpp
    ${AMC_SOURCE_DIR}/src/ljson/json.c
    ${AMC_SOURCE_DIR}/src/ljson/jnum.c
    ${AMC_SOURCE_DIR}/src/ljson/grisu2.c
    ${AMC_SOURCE_DIR}/src/ljson/dragonbox.c)
target_include_directories(
    libabix-format PUBLIC $<BUILD_INTERFACE:${AMC_INCLUDE_DIR}> $<BUILD_INTERFACE:${AMC_SOURCE_DIR}/include/ljson>
                          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libabix-format PUBLIC ABIX::runtime fmt::fmt MICS::MICS ${AMC_EPCD_TARGETS})
target_compile_features(libabix-format PUBLIC cxx_std_17)
set_target_properties(
    libabix-format
    PROPERTIES EXPORT_NAME format
               C_STANDARD 23
               C_STANDARD_REQUIRED ON
               POSITION_INDEPENDENT_CODE ON
               OUTPUT_NAME abix-format)
if(AMC_HAS_EPCD_INTERFACE)
    target_compile_definitions(libabix-format PRIVATE AMC_HAS_EPCD_INTERFACE=1)
endif()
target_compile_definitions(
    libabix-format
    PRIVATE JSON_PARSE_SKIP_COMMENT=1
            JSON_PARSE_LAST_COMMA=1
            JSON_PARSE_SPECIAL_CHAR=1
            JSON_PARSE_SPECIAL_QUOTES=0
            JSON_PARSE_HEX_NUM=0
            JSON_PARSE_SPECIAL_NUM=0
            JSON_PARSE_SPECIAL_DOUBLE=0
            JSON_PARSE_SINGLE_VALUE=1
            JSON_PARSE_FINISHED_CHAR=0
            JSON_ERROR_PRINT_ENABLE=0)

add_library(
    libabix-abi STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/IR/Verify.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/IR/Compatibility.cpp)
target_link_libraries(libabix-abi PUBLIC libabix-format)
set_target_properties(
    libabix-abi
    PROPERTIES EXPORT_NAME abi
               POSITION_INDEPENDENT_CODE ON
               OUTPUT_NAME abix-abi)

add_library(
    amc-platform STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Platform/ELF.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Platform/Symbol.cpp)
target_include_directories(amc-platform PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-platform PUBLIC ABIX::runtime fmt::fmt)
target_compile_features(amc-platform PUBLIC cxx_std_17)
set_target_properties(
    amc-platform
    PROPERTIES EXPORT_NAME platform
               POSITION_INDEPENDENT_CODE ON
               OUTPUT_NAME amc-platform)

add_library(
    libabix-metadata STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Artifact/Metadata.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Artifact/Materialize.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Query/Context.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Query/Query.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Query/Compatibility.cpp)
# The query/context public headers expose llvm::raw_ostream, so the artifact
# library needs LLVM's include paths (and LLVM Support at link time) both for
# its own translation units and for anything that consumes AMC::metadata.
target_link_libraries(libabix-metadata PUBLIC libabix-abi amc-platform amc-llvm)
set_target_properties(
    libabix-metadata
    PROPERTIES EXPORT_NAME metadata
               POSITION_INDEPENDENT_CODE ON
               OUTPUT_NAME abix-metadata)

add_library(
    libabix-tools STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Adapter.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Backend/Lua/Lua.cpp)
target_link_libraries(libabix-tools PUBLIC libabix-metadata)
set_target_properties(
    libabix-tools
    PROPERTIES EXPORT_NAME tools
               POSITION_INDEPENDENT_CODE ON
               OUTPUT_NAME abix-tools)

# M4: semantic -> ABI lowering pass. It links the semantic model to the ABI
# view without rewriting either; backends consume the resulting contract.
add_library(
    amc-pass STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/Lowering.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/Verify.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/Semantic.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/Artifact.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/Compatibility.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/TypeMapping.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Pass/Pipeline.cpp)
target_include_directories(amc-pass PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-pass PUBLIC libabix-format libabix-abi amc-llvm)

# A4 Compilation module: configuration parsing and, later, the shared build
# pipeline. It is a build-tree library consumed by the CLI.
add_library(
    amc-compilation STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Compilation/Config.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Compilation/Provider.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Compilation/Meta.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Compilation/Build.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Compilation/Cache.cpp)
target_include_directories(amc-compilation PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(
    amc-compilation PUBLIC libabix-format libabix-metadata amc-util amc-pass tomlplusplus::tomlplusplus
                          AMC::llvm)
target_compile_definitions(amc-compilation PRIVATE AMC_DEFAULT_PROVIDERS_FILE="${AMC_SOURCE_DIR}/providers.toml")

add_library(AMC::format ALIAS libabix-format)
add_library(AMC::abi ALIAS libabix-abi)
add_library(AMC::metadata ALIAS libabix-metadata)
add_library(AMC::tools ALIAS libabix-tools)
add_library(AMC::platform ALIAS amc-platform)

# AMC helpers (hex/name rendering, JSON text helpers and the structured error
# envelope).
add_library(amc-util STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Names.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Json.cpp
                            ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Error.cpp
                            ${AMC_SOURCE_DIR}/src/SKL/AMC/Support/Process.cpp)
target_include_directories(amc-util PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-util PUBLIC libabix-format fmt::fmt)

add_library(amc-lang STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Lang.cpp
                             ${AMC_SOURCE_DIR}/src/SKL/AMC/Backend/CodeIn.cpp
                             ${AMC_SOURCE_DIR}/src/SKL/AMC/Backend/Codegen.cpp)
target_include_directories(amc-lang PUBLIC ${AMC_INCLUDE_DIR})
target_include_directories(amc-lang PRIVATE ${AMC_GENERATED_INCLUDE_DIR})
target_link_libraries(amc-lang PUBLIC amc-util amc-llvm fmt::fmt tomlplusplus::tomlplusplus)

add_library(amc-core INTERFACE)
target_link_libraries(
    amc-core INTERFACE libabix-format libabix-abi libabix-metadata libabix-tools amc-platform amc-util amc-llvm
                          fmt::fmt)

add_executable(
    amc-cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/C++/C++.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Frontend/Cpp/Frontend.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Frontend/Cpp/Toolchain.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Frontend/Cpp/Native.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Frontend/Cpp/Extractor.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Frontend/Cpp/Thunks.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Backend/Cpp/Backend.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/CppNames.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/Preamble.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/Projection.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/Interfaces.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/Handles.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/Errors.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Private/Backend/Cpp/StaticChecks.cpp)
target_include_directories(amc-cpp PRIVATE ${AMC_INCLUDE_DIR})
target_compile_definitions(amc-cpp PRIVATE ABIX_CLANG_RESOURCE_DIR="${AMC_CLANG_RESOURCE_DIR}")
target_link_libraries(amc-cpp PRIVATE amc-core amc-lang amc-llvm ${AMC_CLANG_CPP_LIBRARY} fmt::fmt
                                      tomlplusplus::tomlplusplus)
# clang-cpp may need the full LLVM dylib even when AMC_LLVM_LINK_LIBRARIES uses
# component libraries; keep the historical explicit target when available.
if(TARGET LLVM)
    target_link_libraries(amc-cpp PRIVATE LLVM)
endif()

add_executable(
    amc-rust ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Rust/Rust.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Frontend/Rust/Frontend.cpp
             ${AMC_SOURCE_DIR}/src/SKL/AMC/Backend/Rust/Backend.cpp)
target_include_directories(amc-rust PRIVATE ${AMC_INCLUDE_DIR} ${AMC_GENERATED_INCLUDE_DIR})
target_link_libraries(amc-rust PRIVATE amc-core amc-lang amc-llvm fmt::fmt)

add_executable(amc-zig ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Zig/Zig.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Backend/Zig/Backend.cpp)
target_include_directories(amc-zig PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-zig PRIVATE amc-core amc-lang amc-llvm fmt::fmt)

add_executable(amc-cancel-test-provider ${AMC_SOURCE_DIR}/tests/providers/cancellation/cancellation_provider.cpp)
target_include_directories(amc-cancel-test-provider PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-cancel-test-provider PRIVATE amc-lang amc-llvm fmt::fmt)
set_target_properties(amc-cancel-test-provider PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(
    amc
    ${AMC_SOURCE_DIR}/main.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/CLI.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Context.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Metadata.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Adapter.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Query.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Symbols.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Verify.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Diff.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Generate.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Cache.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Support.cpp)
target_include_directories(amc PRIVATE ${AMC_INCLUDE_DIR})
target_compile_definitions(amc PRIVATE AMC_DEFAULT_PROVIDERS_FILE="${AMC_SOURCE_DIR}/providers.toml")
target_link_libraries(amc PRIVATE amc-core amc-compilation tomlplusplus::tomlplusplus)

add_executable(
    amc-dump ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/Dump/Dump.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/Dump/Json.cpp
             ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/Dump/Text.cpp)
target_include_directories(amc-dump PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-dump PRIVATE amc-core)

add_executable(
    amc-mcp ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/MCP.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/State.cpp
            ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/Tools.cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/Server.cpp)
target_include_directories(amc-mcp PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-mcp PRIVATE amc-core)

set_target_properties(amc amc-cpp amc-rust amc-zig amc-dump amc-mcp PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                                               ${CMAKE_BINARY_DIR}/bin)
if(AMC_LLVM_DYNAMIC AND AMC_LLVM_LINK_LIBRARIES STREQUAL "LLVM")
    # Keep the LLVM shared-library directory in the installed RPATH so a
    # provider outside the default loader path still finds libLLVM.
    set_target_properties(amc-cpp amc-rust amc-zig PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
endif()
configure_file(${AMC_SOURCE_DIR}/providers.toml ${CMAKE_BINARY_DIR}/bin/amc-providers.toml COPYONLY)

if(AMC_ENABLE_LLDB_PLUGIN)
    find_path(AMC_LLDB_INCLUDE_DIR NAMES lldb/API/LLDB.h)
    find_library(AMC_LLDB_LIBRARY NAMES lldb)
    if(AMC_LLDB_INCLUDE_DIR
       AND AMC_LLDB_LIBRARY
       AND EXISTS ${AMC_SOURCE_DIR}/tools/lldb_abix.cpp)
        add_library(amc-lldb MODULE ${AMC_SOURCE_DIR}/tools/lldb_abix.cpp)
        target_include_directories(amc-lldb PRIVATE ${AMC_INCLUDE_DIR})
        target_include_directories(amc-lldb SYSTEM PRIVATE ${AMC_LLDB_INCLUDE_DIR})
        target_link_libraries(amc-lldb PRIVATE amc-core ${AMC_LLDB_LIBRARY})
        set_target_properties(amc-lldb PROPERTIES OUTPUT_NAME abix_lldb LIBRARY_OUTPUT_DIRECTORY
                                                                        ${CMAKE_BINARY_DIR}/bin)
    else()
        message(STATUS "LLDB not found: the AMC LLDB plugin will not be built")
    endif()
endif()

if(AMC_BUILD_TESTS)
    enable_testing()
endif()

if(AMC_BUILD_AUE AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LUA QUIET lua5.4)
    endif()
    if(NOT LUA_FOUND)
        find_path(LUA_INCLUDE_DIR lua.h PATH_SUFFIXES lua5.4)
        find_library(LUA_LIBRARY NAMES lua5.4 lua)
        if(LUA_INCLUDE_DIR AND LUA_LIBRARY)
            set(LUA_FOUND TRUE)
            set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
            set(LUA_LIBRARIES ${LUA_LIBRARY})
        endif()
    endif()
    if(LUA_FOUND)
        message(STATUS "Lua found: building the Aue conformance prototype")
        add_subdirectory(Aue)
    else()
        message(STATUS "Lua not found: Aue conformance prototype will not be built")
    endif()
endif()

if(AMC_BUILD_BENCHMARKS AND AMC_BENCHMARK_FOUND)
    enable_testing()
    add_subdirectory(bench)
endif()

if(AMC_BUILD_TESTS)
    add_executable(amc-semantic-test ${AMC_SOURCE_DIR}/tests/unit/semantic_model_test.cpp)
    target_include_directories(amc-semantic-test PRIVATE ${AMC_INCLUDE_DIR})
    target_link_libraries(amc-semantic-test PRIVATE amc-core fmt::fmt)
    set_target_properties(amc-semantic-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-semantic-test COMMAND amc-semantic-test)

    add_executable(amc-boundary-test ${AMC_SOURCE_DIR}/tests/unit/module_boundaries_test.cpp)
    target_include_directories(amc-boundary-test PRIVATE ${AMC_INCLUDE_DIR})
    target_link_libraries(amc-boundary-test PRIVATE amc-core fmt::fmt)
    set_target_properties(amc-boundary-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-boundary-test COMMAND amc-boundary-test)

    add_executable(amc-lowering-test ${AMC_SOURCE_DIR}/tests/unit/lowering_test.cpp)
    target_include_directories(amc-lowering-test PRIVATE ${AMC_INCLUDE_DIR})
    target_link_libraries(amc-lowering-test PRIVATE amc-core amc-pass fmt::fmt)
    set_target_properties(amc-lowering-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-lowering-test COMMAND amc-lowering-test)

    add_executable(amc-pass-test ${AMC_SOURCE_DIR}/tests/unit/pass_test.cpp)
    target_include_directories(amc-pass-test PRIVATE ${AMC_INCLUDE_DIR})
    target_link_libraries(amc-pass-test PRIVATE amc-core amc-pass fmt::fmt)
    set_target_properties(amc-pass-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-pass-test COMMAND amc-pass-test)

    find_package(Catch2 3 QUIET)
    if(Catch2_FOUND)
        include(Catch)
        add_executable(amc-core-test ${AMC_SOURCE_DIR}/tests/unit/core_test.cpp)
        target_include_directories(amc-core-test PRIVATE ${AMC_INCLUDE_DIR})
        target_link_libraries(amc-core-test PRIVATE amc-core amc-compilation Catch2::Catch2WithMain)
        set_target_properties(amc-core-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        add_test(NAME amc-core-test COMMAND amc-core-test)

        add_executable(amc-e2e-test ${AMC_SOURCE_DIR}/tests/e2e/e2e_test.cpp)
        target_include_directories(amc-e2e-test PRIVATE ${AMC_INCLUDE_DIR})
        target_link_libraries(amc-e2e-test PRIVATE amc-core Catch2::Catch2WithMain)
        set_target_properties(amc-e2e-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        add_test(NAME amc-e2e-test COMMAND amc-e2e-test)
    else()
        message(STATUS "Catch2 v3 not found — AMC C++ tests will not be built")
    endif()
endif()

if(AMC_INSTALL)
    set(AMC_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/AMC")

    if(AMC_ABIX_FETCHED)
        # Fetched-ABIX mode: the build-tree ABIX checkout is not part of AMC's
        # export set, so install the tools and headers only. The CMake package
        # (and its exported artifact libraries) requires an installed ABIX
        # package; reconfigure with -DABIX_DIR to generate it.
        message(STATUS "Fetched ABIX mode: installing AMC tools/headers without the CMake package export")
        install(TARGETS amc amc-cpp amc-rust amc-zig amc-dump amc-mcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        install(
            TARGETS amc amc-cpp amc-rust amc-zig amc-dump amc-mcp
            EXPORT AMCTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(
            TARGETS libabix-format libabix-abi libabix-metadata libabix-tools amc-platform amc-llvm
            EXPORT AMCTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

        install(
            EXPORT AMCTargets
            FILE AMCTargets.cmake
            NAMESPACE AMC::
            DESTINATION ${AMC_CMAKE_INSTALL_DIR})

        configure_package_config_file(
            ${AMC_SOURCE_DIR}/cmake/AMCConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/AMCConfig.cmake
            INSTALL_DESTINATION ${AMC_CMAKE_INSTALL_DIR}
            PATH_VARS CMAKE_INSTALL_INCLUDEDIR)

        write_basic_package_version_file(
            ${CMAKE_CURRENT_BINARY_DIR}/AMCConfigVersion.cmake
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMinorVersion)

        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AMCConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/AMCConfigVersion.cmake
                      ${AMC_SOURCE_DIR}/cmake/AMC.cmake DESTINATION ${AMC_CMAKE_INSTALL_DIR})
    endif()

    install(
        FILES ${AMC_SOURCE_DIR}/providers.toml
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME amc-providers.toml)
    install(DIRECTORY ${AMC_CODEIN_DIR}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/AMC/CodeIn)
    install(DIRECTORY ${AMC_SOURCE_DIR}/include/SKL/AMC DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SKL
            PATTERN "Private" EXCLUDE)
    if(AMC_INSTALL_PRIVATE_HEADERS)
        # Private implementation headers are only useful to embedders that build
        # against AMC internals; they are opt-in and never part of the public
        # include surface.
        install(DIRECTORY ${AMC_SOURCE_DIR}/include/SKL/AMC/Private
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SKL/AMC)
    endif()
    install(DIRECTORY ${AMC_SOURCE_DIR}/include/ljson DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${AMC_SOURCE_DIR}/LICENSE ${AMC_SOURCE_DIR}/NOTICE ${AMC_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/AMC)
endif()

find_program(AMC_PYTHON_EXECUTABLE NAMES python3 python)
if(AMC_BUILD_TESTS
   AND AMC_INSTALL
   AND AMC_PYTHON_EXECUTABLE
   AND NOT AMC_ABIX_FETCHED)
    add_test(NAME amc-package-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/amc_package_test.py ${CMAKE_BINARY_DIR}
                     ${AMC_SOURCE_DIR} --abix-dir=${ABIX_DIR} --fmt-dir=${fmt_DIR} --cxx=${CMAKE_CXX_COMPILER})
    set_tests_properties(amc-package-test PROPERTIES TIMEOUT 180)
    if(DEFINED ENV{KEEP_ARTIFACTS} AND "$ENV{KEEP_ARTIFACTS}" STREQUAL "1")
        set_tests_properties(amc-package-test PROPERTIES ENVIRONMENT "KEEP_ARTIFACTS=1")
    endif()
endif()

if(AMC_BUILD_TESTS AND AMC_PYTHON_EXECUTABLE)
    set(_amc_test_env "ABIX_INCLUDE_DIRS=${AMC_ABIX_INCLUDE_DIRS_ENV}" "CXX=${CMAKE_CXX_COMPILER}")

    add_test(NAME amc-feature-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/feature_test.py
                                           ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-rust-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/providers/rust/conformance_test.py
                     ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-rust-layout-probe-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/providers/rust/layout_probe_test.py
                     ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-rust-panic-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/interop/panic_test.py
                                              ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-cancellation-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/providers/cancellation/cancellation_test.py
                     ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-interop-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/interop/interop_test.py
                                           ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-expose-interop-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/interop/expose_test.py ${CMAKE_BINARY_DIR}/bin
                     ${AMC_SOURCE_DIR})
    add_test(NAME amc-m7-interop-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/interop/m7/m7_test.py
                                              ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-cmake-integration-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/cmake_integration_test.py
                     ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-elf-pe-metadata-test
             COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/elf_pe_metadata_test.py
                     ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-bootstrap-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/bootstrap_test.py
                                             ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-boundary-contracts COMMAND ${AMC_PYTHON_EXECUTABLE}
                                                 ${AMC_SOURCE_DIR}/tools/check_module_boundaries.py --contracts-only)
    add_test(NAME amc-boundary-edges COMMAND ${AMC_PYTHON_EXECUTABLE}
                                             ${AMC_SOURCE_DIR}/tools/check_module_boundaries.py --strict)
    set_tests_properties(amc-boundary-contracts amc-boundary-edges PROPERTIES TIMEOUT 60)

    set_tests_properties(
        amc-feature-test
        amc-rust-test
        amc-rust-panic-test
        amc-cancellation-test
        amc-interop-test
        amc-expose-interop-test
        amc-m7-interop-test
        amc-cmake-integration-test
        amc-elf-pe-metadata-test
        amc-bootstrap-test
        PROPERTIES ENVIRONMENT "${_amc_test_env}" TIMEOUT 180)
endif()
