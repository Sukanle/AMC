# Standalone Aue conformance/benchmark build.
#
# This entry point does not require the AMC driver or its generated contract:
# it consumes the checked-in fallback contract in Aue/src/Counter.cpp and only
# needs an installed ABIX runtime, fmt and Lua 5.4.

cmake_minimum_required(VERSION 3.20)
project(AueStandalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ABIX 0.3 CONFIG COMPONENTS runtime REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Lua REQUIRED)

# FindLua provides LUA_INCLUDE_DIR/LUA_LIBRARIES; Aue/CMakeLists also accepts
# the plural spelling used by some toolchains.
if(LUA_INCLUDE_DIR AND NOT LUA_INCLUDE_DIRS)
    set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
endif()

set(AMC_BUILD_TESTS ON)
enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. aue)
