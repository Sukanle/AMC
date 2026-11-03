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

#include "SKL/AMC/Lang/Lang.h"   // IWYU pragma: keep
#include <vector>
#include <string>

namespace amc::lang::cpp {

// Clang resource directory (e.g. /opt/homebrew/opt/llvm/lib/clang/22),
// detected at build time via `clang -print-resource-dir` and injected
// as a compile definition.  Used to locate built-in headers (stdarg.h,
// stddef.h, etc.) and the Brew LLVM's libc++ installation.
extern const char * const kClangResourceDir;

int run_frontend(const char *config_path, const char *output_path);

int backend(const char *input, const char *output);

void append_macos_sysroot(std::vector<std::string> &flags);

// Non-Apple targets: point the clang we are linked against at the resource
// directory detected at build time, so the built-in headers (stddef.h,
// stdarg.h, ...) are found without a .abic.toml naming a toolchain version.
void append_clang_resource_dir(std::vector<std::string> &flags);

}   // namespace amc::lang::cpp
