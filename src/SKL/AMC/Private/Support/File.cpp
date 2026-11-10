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
#include "SKL/AMC/Private/Support/File.h"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

#include <chrono>
#include <utility>

namespace skl::amc::support {

llvm::ErrorOr<std::string> read_text_file(llvm::StringRef path) {
    auto buffer = llvm::MemoryBuffer::getFile(path, /*IsText=*/true);
    if (!buffer) return buffer.getError();
    return (*buffer)->getBuffer().str();
}

llvm::ErrorOr<std::vector<uint8_t>> read_binary_file(llvm::StringRef path) {
    auto buffer = llvm::MemoryBuffer::getFile(path, /*IsText=*/false);
    if (!buffer) return buffer.getError();
    const llvm::StringRef data = (*buffer)->getBuffer();
    return std::vector<uint8_t>(data.bytes_begin(), data.bytes_end());
}

llvm::Error write_text_file(llvm::StringRef path, llvm::StringRef text) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
    if (ec) return llvm::errorCodeToError(ec);
    out << text;
    out.close();
    if (out.has_error()) return llvm::errorCodeToError(out.error());
    return llvm::Error::success();
}

llvm::Error write_binary_file(llvm::StringRef path, llvm::ArrayRef<uint8_t> bytes) {
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
    if (ec) return llvm::errorCodeToError(ec);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    out.close();
    if (out.has_error()) return llvm::errorCodeToError(out.error());
    return llvm::Error::success();
}

llvm::Error make_directories(llvm::StringRef path) {
    if (path.empty()) return llvm::Error::success();
    const std::error_code ec = llvm::sys::fs::create_directories(path);
    if (ec) return llvm::errorCodeToError(ec);
    return llvm::Error::success();
}

bool path_exists(llvm::StringRef path) { return llvm::sys::fs::exists(path); }

bool is_regular_file(llvm::StringRef path) { return llvm::sys::fs::is_regular_file(path); }

bool is_directory(llvm::StringRef path) { return llvm::sys::fs::is_directory(path); }

llvm::Error remove_path(llvm::StringRef path, bool recursive) {
    const std::error_code ec = recursive ? llvm::sys::fs::remove_directories(path) : llvm::sys::fs::remove(path);
    if (ec) return llvm::errorCodeToError(ec);
    return llvm::Error::success();
}

llvm::Error rename_path(llvm::StringRef from, llvm::StringRef to) {
    const std::error_code ec = llvm::sys::fs::rename(from, to);
    if (ec) return llvm::errorCodeToError(ec);
    return llvm::Error::success();
}

llvm::ErrorOr<uint64_t> file_size(llvm::StringRef path) {
    uint64_t size = 0;
    const std::error_code ec = llvm::sys::fs::file_size(path, size);
    if (ec) return ec;
    return size;
}

llvm::ErrorOr<std::vector<DirectoryEntryInfo>> list_directory(llvm::StringRef path) {
    std::error_code ec;
    std::vector<DirectoryEntryInfo> entries;
    for (llvm::sys::fs::directory_iterator it(path, ec), end; it != end; it.increment(ec)) {
        if (ec) return ec;
        const auto status = it->status();
        if (!status) return status.getError();
        DirectoryEntryInfo entry;
        entry.path = it->path();
        entry.size = status->getSize();
        entry.modification_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(status->getLastModificationTime().time_since_epoch())
                .count();
        entry.directory = status->type() == llvm::sys::fs::file_type::directory_file;
        entries.push_back(std::move(entry));
    }
    if (ec) return ec;
    return entries;
}

std::string error_message(llvm::Error error) { return llvm::toString(std::move(error)); }

}   // namespace skl::amc::support
