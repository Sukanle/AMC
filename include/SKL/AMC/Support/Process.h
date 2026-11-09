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

#include <cstdint>
#include <string>

namespace skl::amc::support {

// OS process id. Used to keep temporary artifact and anchor paths unique when
// a build system launches several `amc` processes for the same project.
uint64_t process_id() noexcept;

// A process-unique, monotonically increasing suffix for temporary paths and
// cache staging names.
std::string unique_process_suffix();

}   // namespace skl::amc::support
