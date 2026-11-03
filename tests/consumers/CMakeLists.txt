# Standalone CMake project for the AMC self-description consumers.
#
# It exists so `tests/cli/bootstrap_test.py` and anyone reproducing the
# bootstrap by hand drive the very same pipeline the library documents:
#
#   self.abic.toml --(amc build)--> build/amc_core.abix
#                  --(amc generate -l cpp)--> generated/amc_core.hpp
#                  --(C++17 compile)--> self_consumer
#
# amc_add_abi owns the first two steps and attaches both outputs to the
# target's incremental build graph, so touching an `.abic.toml` dependency
# re-runs the metadata pipeline instead of silently reusing a stale header.
#
# Configure:
#   cmake -S tests/consumers -B <build> -G Ninja \
#     -DAMC_MODULE_DIR=<amc-root>/CMake \
#     -DAMC_SOURCE_ROOT=<amc-root> \
#     -DAMC_EXECUTABLE=<dir>/amc \
#     -DABIX_INCLUDE_DIRS="<abix>;<mics>;<hlmd>"

cmake_minimum_required(VERSION 3.20)
project(AMCSelfConsumers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT AMC_SOURCE_ROOT)
  message(FATAL_ERROR "AMC_SOURCE_ROOT must point at the AMC source tree")
endif()

include("${AMC_MODULE_DIR}/AMC.cmake")

add_executable(self_consumer self_consumer.cpp)

# self_consumer.cpp reaches for `SKL/AMC/Core/Core.h`, which lives outside the
# include root amc_add_abi contributes (that root only serves `SKL/ABIX/...`).
target_include_directories(self_consumer PRIVATE "${AMC_SOURCE_ROOT}/include")

amc_add_abi(
    TARGET
    self_consumer
    CONFIG
    "${AMC_SOURCE_ROOT}/self.abic.toml"
    ABIX_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/build/amc_core.abix"
    CPP_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/amc_core.hpp"
    DEPENDS
    "${AMC_SOURCE_ROOT}/self_types.cpp"
    "${AMC_SOURCE_ROOT}/include/SKL/AMC/Core/Core.h")
