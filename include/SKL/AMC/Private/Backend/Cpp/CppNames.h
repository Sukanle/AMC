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

#include "SKL/AMC/Core/Core.h"

#include <cstdint>
#include <string>

namespace skl::amc::lang::backend::cpp {

// C++ spelling helpers shared by the projection emitters. Everything here maps
// ABIX names and types to valid C++ text; nothing reads or writes files.

std::string cpp_name(std::string s);

bool is_cpp_identifier(const std::string &s);

// A name that can be pasted into a generated `sizeof`/`offsetof` check: a C++
// identifier, optionally qualified with `::`. This deliberately rejects the
// mangled names AMC gives to implicit/unnamed types (`struct Foo *`, anonymous
// enums) so the ABI check header never emits uncompilable code.
bool is_cpp_qualified_name(const std::string &s);

std::string cpp_string(const std::string &value);

std::string hex_u64(uint64_t value);

// Render a recorded TypeID as a C++ spelling. Pointer and record names are
// already source-level spellings; a C primitive that carries the C spelling
// `_Bool` maps to the C++ `bool`, exactly as the generated thunk source does.
std::string cpp_type_of(const amc::ABIModule &m, amc::Hash128 id);

bool type_is_pointer(const amc::ABIModule &m, amc::Hash128 id);

// An interface method is usable as an object-handle call only when its first
// parameter is a `void *`/`const void *` pointer (the object handle). The
// generated proxy class is emitted only when every method qualifies.
bool interface_has_handle_methods(const amc::ABIModule &m, const amc::InterfaceRecord &record);

}   // namespace skl::amc::lang::backend::cpp
