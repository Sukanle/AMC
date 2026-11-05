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
#include "SKL/AMC/Backend/CodeIn.h"

#include "SKL/AMC/Config.h"

#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

namespace amc::backend {
namespace {

std::mutex &codein_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, std::string> &codein_cache() {
    static std::unordered_map<std::string, std::string> cache;
    return cache;
}

llvm::SmallString<256> executable_directory() {
#if defined(_WIN32)
    llvm::SmallVector<char, 4'096> buffer(4'096, 0);
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        llvm::SmallString<256> path(llvm::StringRef(buffer.data(), length));
        llvm::sys::path::remove_filename(path);
        return path;
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    llvm::SmallVector<char, 4'096> buffer(size, 0);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        llvm::SmallString<256> path(llvm::StringRef(buffer.data()));
        llvm::SmallString<256> canonical;
        if (!llvm::sys::fs::real_path(path, canonical)) {
            llvm::sys::path::remove_filename(canonical);
            return canonical;
        }
        llvm::sys::path::remove_filename(path);
        return path;
    }
#else
    llvm::SmallVector<char, 4'096> buffer(4'096, 0);
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0) {
        llvm::SmallString<256> path(llvm::StringRef(buffer.data(), static_cast<size_t>(length)));
        llvm::sys::path::remove_filename(path);
        return path;
    }
#endif
    return {};
}

bool read_codein_uncached(llvm::StringRef relative, std::string &text, std::string &error, std::string &searched) {
    llvm::SmallVector<llvm::SmallString<256>, 4> candidates;
    const auto add_candidate = [&](llvm::StringRef base) {
        if (base.empty()) return;
        llvm::SmallString<256> path(base);
        llvm::sys::path::append(path, relative);
        candidates.push_back(std::move(path));
    };

    add_candidate(AMC_CODEIN_DIR);
    if (const llvm::SmallString<256> executable = executable_directory(); !executable.empty()) {
        llvm::SmallString<256> install(executable);
        llvm::sys::path::append(install, "..", AMC_CODEIN_INSTALL_SUBDIR, relative);
        candidates.push_back(std::move(install));
        llvm::SmallString<256> beside(executable);
        llvm::sys::path::append(beside, "CodeIn", relative);
        candidates.push_back(std::move(beside));
    }

    for (const auto &candidate : candidates) {
        if (auto buffer = llvm::MemoryBuffer::getFile(candidate)) {
            text = (*buffer)->getBuffer().str();
            return true;
        }
    }

    searched.clear();
    for (const auto &candidate : candidates) {
        if (!searched.empty()) searched += ", ";
        searched += candidate.str().str();
    }
    error = "cannot read CodeIn asset: ";
    error += relative.str();
    error += " (searched ";
    error += searched;
    error += ")";
    return false;
}

}   // namespace

bool read_codein_text(std::string_view relative, std::string &text, std::string &error) {
    const std::string key(relative);
    {
        std::lock_guard<std::mutex> lock(codein_mutex());
        const auto found = codein_cache().find(key);
        if (found != codein_cache().end()) {
            text = found->second;
            error.clear();
            return true;
        }
    }

    std::string searched;
    if (!read_codein_uncached(llvm::StringRef(key), text, error, searched)) return false;

    std::lock_guard<std::mutex> lock(codein_mutex());
    codein_cache()[key] = text;
    error.clear();
    return true;
}

std::string read_codein_text(std::string_view relative) {
    std::string text;
    std::string error;
    if (!read_codein_text(relative, text, error)) throw std::runtime_error(error);
    return text;
}

}   // namespace amc::backend
