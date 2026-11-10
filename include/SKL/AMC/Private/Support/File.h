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
#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorOr.h>

#include <cstdint>
#include <string>
#include <vector>

namespace skl::amc::support {

// Portable file helpers built on LLVM Support. Paths are UTF-8/StringRef on
// every platform; callers never hand a std::filesystem::path to a std::string
// API (path::string_type is std::wstring on Windows).
llvm::ErrorOr<std::string> read_text_file(llvm::StringRef path);
llvm::ErrorOr<std::vector<uint8_t>> read_binary_file(llvm::StringRef path);

llvm::Error write_text_file(llvm::StringRef path, llvm::StringRef text);
llvm::Error write_binary_file(llvm::StringRef path, llvm::ArrayRef<uint8_t> bytes);

llvm::Error make_directories(llvm::StringRef path);
bool path_exists(llvm::StringRef path);

// Render an llvm::Error for the CLI's fail()/detail path and consume it.
std::string error_message(llvm::Error error);

}   // namespace skl::amc::support
