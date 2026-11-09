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
#ifndef AMC_UTIL_JSON_H
#define AMC_UTIL_JSON_H

#include <string>

namespace skl::amc::util {

// Minimal JSON text helpers for hand-written emitters (LLVM-style output in
// `amc-dump`). The structured `amc::Json` type remains the default elsewhere.
std::string escape_json(const std::string &value);
void json_string(std::string &output, const std::string &value);
void indent(std::string &output, unsigned depth);

}   // namespace skl::amc::util

#endif   // AMC_UTIL_JSON_H
