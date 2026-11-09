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
#ifndef AMC_COMPILATION_META_H
#define AMC_COMPILATION_META_H

#include "SKL/AMC/Core/Core.h"

#include <filesystem>
#include <string>

namespace skl::amc::compilation {

// Emit the `.abix.meta` sidecar used by symbol servers and CI indexing.
bool write_abix_meta(const ABIModule &module, const std::filesystem::path &artifact, std::string &error);

}   // namespace skl::amc::compilation

#endif   // AMC_COMPILATION_META_H
