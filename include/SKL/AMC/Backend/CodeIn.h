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
#ifndef AMC_BACKEND_CODEIN_H
#define AMC_BACKEND_CODEIN_H

#include <string>
#include <string_view>

namespace skl::amc::backend {

// Shared CodeIn asset loader for language backends (C++, Rust, Zig).
//
// The build-tree path from Config.h is tried first; installed providers fall
// back to the install-relative data directory and to a CodeIn directory next to
// the executable. Results are cached for the life of the process.
bool read_codein_text(std::string_view relative, std::string &text, std::string &error);

// Convenience overload for callers that already treat a missing asset as a
// fatal backend error. Throws std::runtime_error.
std::string read_codein_text(std::string_view relative);

}   // namespace skl::amc::backend

#endif   // AMC_BACKEND_CODEIN_H
