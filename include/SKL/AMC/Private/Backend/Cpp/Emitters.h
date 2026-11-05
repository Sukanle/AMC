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

#include <string>

#include <fmt/os.h>
#include <optional>
#include <llvm/ADT/StringRef.h>

namespace amc {
struct AbiModule;
struct ErrorSet;
struct Hash128;
struct Function;
struct InterfaceRecord;
namespace ir {
struct LoweringResult;
struct LoweredExposedType;
}   // namespace ir
}   // namespace amc

namespace amc::lang::backend::cpp {

// Emitters share the same status convention as `backend`: 0 on success, 1 on a
// reported error (message already stored in `error`), or
// `amc::lang::kCapabilityCancelled` when the operation was cancelled.

// Includes, warning banner and the CodeIn prologue.
class Emit {
public:
    Emit(fmt::ostream &o, const amc::AbiModule &m)
        : _o(o)
        , _m(m) {}

    int preamble(std::string &error);

    // Types, functions, canonical tables, names section, metadata region and maps.
    int projection(std::string &error);

    // Interface contracts, traits and proxy classes.
    void interfaces();

    // Exposed-handle RAII wrappers from the lowering result.
    void handles(const amc::ir::LoweringResult &lowering);

    // Named error sets with stable numeric codes.
    void named_error_sets();

    // Typed `Result<T, E>` error-boundary wrappers.
    void error_boundaries();

    // Native `TypeTraits` specialisations guarded by
    // `AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS`.
    void native_type_traits();

    // Separate `amc_abi_check.hpp` header next to `output`.
    void static_checks(const char *output);

private:
    void error_set(const amc::ErrorSet &set);
    const amc::Function *find_function(amc::Hash128 signature);
    std::string self_type_of(
        const amc::Function *create, const amc::Function *destroy, const amc::ir::LoweredExposedType &record);
    void decl(const amc::Function *function);
    void handle(const amc::ir::LoweredExposedType &record, const amc::Function *create, const amc::Function *destroy,
        const std::string &self_type);
    void contract(const amc::InterfaceRecord &record);
    void types();
    void functions();
    void tables();
    void names_section();
    int metadata_region(std::string &error);
    void maps();

    fmt::ostream &_o;
    const amc::AbiModule &_m;
};
}   // namespace amc::lang::backend::cpp
