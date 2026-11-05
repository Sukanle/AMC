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
#ifndef AMC_QUERY_H
#define AMC_QUERY_H

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Verify.h"

#include <string>
#include <string_view>
#include <vector>

namespace amc {

// AI-PS: the AMC query engine.
//
// `amc query` and the future MCP server share this layer so there is exactly
// one implementation of "find a type / find a function / compare two modules".
// Results are addressed by index into the AbiModule so callers can render
// text, JSON or a protocol message from the same data.

// Resolves a needle (exact name, then substring; or a 0x/32-hex TypeID) to
// type indices, in module order.
std::vector<size_t> query_type_indices(const AbiModule &module, std::string_view needle);
std::vector<size_t> query_function_indices(const AbiModule &module, std::string_view needle);

// Resolves a type id to its declared name, or nullptr when unknown.
const Type *find_type_by_id(const AbiModule &module, Hash128 id);

std::string query_types_text(const AbiModule &module, const std::vector<size_t> &indices, bool include_layout);
std::string query_types_json(
    const AbiModule &module, const std::vector<size_t> &indices, bool include_layout, std::string_view needle);
std::string query_functions_text(const AbiModule &module, const std::vector<size_t> &indices);
std::string query_functions_json(const AbiModule &module, const std::vector<size_t> &indices, std::string_view needle);
// "Is `target` a safe replacement for `source`?" — strict: every type, layout,
// field set and function signature in `source` must exist unchanged in
// `target`. Additions in `target` are reported as drift too.
std::string compatibility_query_text(const AbiModule &source, const AbiModule &target, const VerifyResult &result);
std::string compatibility_query_json(const AbiModule &source, const AbiModule &target, const VerifyResult &result);

}   // namespace amc

#endif
