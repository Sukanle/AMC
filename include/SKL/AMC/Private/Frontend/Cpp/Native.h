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

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>

#include <cstdint>
#include <string>

namespace skl::amc::lang::frontend::cpp {

// The native C++ kinds an `[[adapt]]` may declare. Each maps a canonical STL
// spelling onto an ABIX-STD boundary contract through a generated adapter.
enum class NativeKind : uint32_t {
    NONE = 0,
    STRING = 1,        // std::STRING        -> STRING-view (owned copy in the adapter)
    STRING_VIEW = 2,   // std::STRING_VIEW   -> STRING-view (borrowed)
    VECTOR = 3,        // std::VECTOR<T>     -> sequence-view (owned copy in the adapter)
    SPAN = 4           // std::SPAN<const T> -> sequence-view (borrowed)
};

// One recorded native↔ABIX-STD parameter adaptation. The function keeps the
// native parameter type (the method's real signature); `[[expose]]` substitutes
// the boundary type in the generated thunk and emits the marshalling step. This
// is a frontend working set consumed by `ThunkEmitter`; it is not serialized and
// never feeds ABI identity.
struct NativeAdapt {
    amc::Hash128 native_type{};     // recorded native parameter type
    amc::Hash128 boundary_type{};   // ABIX-STD boundary type (filled by expose)
    amc::Hash128 element_id{};      // element/value type for a sequence view
    uint32_t kind = 0;              // NativeKind
    std::string element;            // element spelling (sequence views)
    std::string target;             // declared boundary type name
};

NativeKind native_kind_of(const std::string &name);

// Normalize a type spelling for element matching: drop whitespace and the
// qualifiers that do not change the element's ABI identity.
std::string normalise_element(const std::string &spelling);

// The native type name recorded for a native parameter (distinct per element,
// so `std::vector<int>` and `std::vector<float>` never collapse onto one type).
std::string native_type_name(NativeKind kind, const std::string &element);

// Parse a fixed-width integer spelling (`int32_t`, `uint16_t`, ...) into its
// byte size and signedness. Returns false when the spelling is not one, so the
// matcher never guesses at an element type it cannot prove.
bool fixed_width_int(const std::string &spelling, uint32_t &size, bool &is_signed);

// ABI descriptor for a primitive scalar type.
//
// Identity is the *abstract C ABI kind* — the width and format are recorded as
// properties, not baked into the name. Returns an empty string when the type is
// not a scalar builtin (pointer, array, record, ...), meaning the caller keeps
// the spelling-based identity. `abi` is filled with the packed PrimitiveABIKind
// + width + format.
std::string primitive_abi_key(clang::ASTContext &ctx, clang::QualType qt, uint32_t &abi);

}   // namespace skl::amc::lang::frontend::cpp
