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
#ifndef AMC_UTIL_ERROR_H
#define AMC_UTIL_ERROR_H

#include <string>

#include "SKL/AMC/Diagnostics/Error.h"

namespace skl::amc::util {

// Process-wide error envelope selector, set once from `--error-format`.
extern amc::ErrorFormat g_error_format;
// True when `--error-format` was seen; `--format json` must not override it.
extern bool g_error_format_explicit;
// True when `--error-format` had an unsupported value.
extern bool g_error_format_invalid;

// Prints `error` through the selected envelope and returns 1 (convenience for
// `return fail(...)` paths). Callers pass the file/detail that located the
// failure when available.
int fail(amc::ErrorCategory category, const std::string &stage, const std::string &message,
    const std::string &file = {}, const std::string &detail = {});

int usage_error(const std::string &message);

// Maps a free-form load error to the right category (I/O vs format).
amc::ErrorCategory read_error_category(const std::string &message);

}   // namespace skl::amc::util

#endif   // AMC_UTIL_ERROR_H
