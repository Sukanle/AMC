# Standalone Aue conformance/benchmark build.
#
# This entry point does not require the AMC driver or its generated contract:
# it consumes the checked-in fallback contract in Aue/src/Counter.cpp and only
# needs an installed ABIX runtime, fmt and Lua 5.4.

cmake_minimum_required(VERSION 3.20)
project(AueStandalone VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)

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

install(TARGETS abix-conformance abix-bench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
    FILES ${CMAKE_CURRENT_LIST_DIR}/../README.md ${CMAKE_CURRENT_LIST_DIR}/../Conformance.lua
          ${CMAKE_CURRENT_LIST_DIR}/../Edges.lua
    DESTINATION ${CMAKE_INSTALL_DATADIR}/aue)

set(CPACK_PACKAGE_NAME "Aue")
set(CPACK_PACKAGE_VENDOR "Sukanle")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "Aue-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_GENERATOR "TGZ")
include(CPack)
