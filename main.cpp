/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "SKL/AMC/CLI/CLI.h"

#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

namespace {

// Resolve the directory that contains the running `amc` binary. `argv[0]` can
// be a bare name when the tool is invoked through PATH; `fs::absolute(argv[0])`
// would then point at the current directory and provider lookup would fail.
std::filesystem::path executable_directory() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    std::vector<char> buffer(4'096, 0);
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) return fs::path(std::string(buffer.data(), length)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size, 0);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        fs::path path = fs::weakly_canonical(fs::path(buffer.data()), ec);
        if (!ec) return path.parent_path();
        return fs::path(buffer.data()).parent_path();
    }
#else
    std::vector<char> buffer(4'096, 0);
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0) return fs::path(std::string(buffer.data(), static_cast<size_t>(length))).parent_path();
#endif
    return {};
}

}   // namespace

int main(int argc, char **argv) {
    std::filesystem::path executable_dir = executable_directory();
    if (executable_dir.empty()) {
        const std::filesystem::path program = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path("amc");
        executable_dir = std::filesystem::absolute(program).parent_path();
    }
    return skl::amc::cli::run(argc, argv, executable_dir);
}
