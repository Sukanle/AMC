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
#ifndef AMC_COMPILATION_BUILD_H
#define AMC_COMPILATION_BUILD_H

#include <filesystem>

namespace skl::amc::compilation {

// `amc build` entry point. The CLI only parses/forwards arguments; the
// compilation pipeline lives here.
int build_command(char **args, int arg_count, const std::filesystem::path &provider);

}   // namespace skl::amc::compilation

#endif   // AMC_COMPILATION_BUILD_H
