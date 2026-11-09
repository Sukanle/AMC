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
#ifndef AMC_IR_LOWERING_H
#define AMC_IR_LOWERING_H

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Semantic/Model.h"

#include <stdint.h>

#include <string>
#include <vector>

namespace skl::amc::ir {

// ABI identities. A type is identified by its stable TypeID; a function by its
// ABI signature hash. These are deliberately separate from the semantic ids in
// `semantic::Model` — lowering links the two, it never replaces either.
using AbiTypeId = Hash128;
using AbiFunctionId = Hash128;

enum class LoweringStatus : uint32_t {
    LOWERED,    // a corresponding ABI entity exists
    OPAQUE,     // representable, but only as an OPAQUE handle
    REJECTED,   // no safe ABI mapping
};

enum class LoweringReason : uint32_t {
    OK = 0,
    MISSING_ABI_TYPE,
    MISSING_ABI_FUNCTION,
    AMBIGUOUS_ABI_FUNCTION,
    MISSING_SEMANTIC_RECORD,
    LAYOUT_UNVERIFIED,
    OWNERSHIP_UNRESOLVED,
    TEMPLATE_NOT_MATERIALIZED,
};

struct LoweredBase {
    semantic::SemanticTypeId semantic{};
    AbiTypeId abi{};
    uint32_t bit_offset = 0;
    bool is_virtual = false;
};

struct LoweredField {
    std::string name;
    AbiTypeId type{};
    uint32_t bit_offset = 0;
    uint32_t bit_width = 0;
};

struct LoweredType {
    semantic::SemanticTypeId semantic{};
    AbiTypeId abi{};
    uint32_t abi_index = UINT32_MAX;
    std::string name;
    LoweringStatus status = LoweringStatus::REJECTED;
    LoweringReason reason = LoweringReason::MISSING_ABI_TYPE;
    uint32_t size = 0;
    uint32_t align = 0;
    Hash128 layout_hash{};
    std::vector<LoweredBase> bases;
    std::vector<LoweredField> fields;
};

struct LoweredParameter {
    AbiTypeId type{};
    uint32_t flags = 0;
};

struct LoweredFunction {
    semantic::SemanticFuncId semantic{};
    AbiFunctionId abi{};
    uint32_t abi_index = UINT32_MAX;
    std::string name;
    LoweringStatus status = LoweringStatus::REJECTED;
    LoweringReason reason = LoweringReason::MISSING_ABI_FUNCTION;
    Hash128 signature{};
    AbiTypeId return_type{};
    std::vector<LoweredParameter> parameters;
};

struct LoweredOwnership {
    semantic::SemanticFuncId function{};
    AbiFunctionId abi{};
    semantic::InteractionKind interaction = semantic::InteractionKind::PARAM;
    uint32_t index = 0;
    semantic::OwnershipKind ownership = semantic::OwnershipKind::UNSPECIFIED;
    semantic::LifetimeKind lifetime = semantic::LifetimeKind::UNKNOWN;
    semantic::Mutability mutability = semantic::Mutability::IMMUTABLE;
    AbiFunctionId release{};
    bool matched_abi_contract = false;
};

struct LoweredExposedMethod {
    std::string name;
    std::string symbol;
    AbiFunctionId abi{};
};

// Lowered `[[expose]]` capability: the opaque handle type, its create/destroy
// entry points and the thunk methods a consumer language can wrap. The method
// list is explicit; a backend never recovers it from the thunk spelling.
struct LoweredExposedType {
    semantic::SemanticTypeId semantic{};
    AbiTypeId abi{};
    std::string name;
    std::string symbol;
    LoweringStatus status = LoweringStatus::REJECTED;
    LoweringReason reason = LoweringReason::MISSING_ABI_TYPE;
    AbiFunctionId create{};
    AbiFunctionId destroy{};
    bool has_constructor = false;
    bool opaque = true;
    std::vector<LoweredExposedMethod> methods;
};

struct LoweredTemplateInstance {
    semantic::TemplateInstId instance{};
    Hash128 result{};
    AbiTypeId abi_type{};
    AbiFunctionId abi_function{};
    LoweringStatus status = LoweringStatus::REJECTED;
    LoweringReason reason = LoweringReason::TEMPLATE_NOT_MATERIALIZED;
};

struct LoweringResult {
    std::vector<LoweredType> types;
    std::vector<LoweredFunction> functions;
    std::vector<LoweredOwnership> ownership;
    std::vector<LoweredExposedType> exposed;
    std::vector<LoweredTemplateInstance> templates;
    uint32_t rules_version = 1;

    [[nodiscard]] bool has_rejections() const {
        for (const auto &type : types)
            if (type.status == LoweringStatus::REJECTED) return true;
        for (const auto &function : functions)
            if (function.status == LoweringStatus::REJECTED) return true;
        for (const auto &exposed : exposed)
            if (exposed.status == LoweringStatus::REJECTED) return true;
        for (const auto &instance : templates)
            if (instance.status == LoweringStatus::REJECTED) return true;
        return false;
    }
};

}   // namespace skl::amc::ir

#endif   // AMC_IR_LOWERING_H
