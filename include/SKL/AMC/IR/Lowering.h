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

namespace amc::ir {

// ABI identities. A type is identified by its stable TypeID; a function by its
// ABI signature hash. These are deliberately separate from the semantic ids in
// `semantic::Model` — lowering links the two, it never replaces either.
using AbiTypeId = Hash128;
using AbiFunctionId = Hash128;

enum class LoweringStatus : uint32_t {
    lowered,   // a corresponding ABI entity exists
    opaque,    // representable, but only as an opaque handle
    rejected   // no safe ABI mapping
};

enum class LoweringReason : uint32_t {
    ok = 0,
    missing_abi_type,
    missing_abi_function,
    ambiguous_abi_function,
    missing_semantic_record,
    layout_unverified,
    ownership_unresolved,
    template_not_materialized
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
    LoweringStatus status = LoweringStatus::rejected;
    LoweringReason reason = LoweringReason::missing_abi_type;
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
    semantic::SemanticFunctionId semantic{};
    AbiFunctionId abi{};
    uint32_t abi_index = UINT32_MAX;
    std::string name;
    LoweringStatus status = LoweringStatus::rejected;
    LoweringReason reason = LoweringReason::missing_abi_function;
    Hash128 signature{};
    AbiTypeId return_type{};
    std::vector<LoweredParameter> parameters;
};

struct LoweredOwnership {
    semantic::SemanticFunctionId function{};
    AbiFunctionId abi{};
    semantic::InteractionKind interaction = semantic::InteractionKind::parameter;
    uint32_t index = 0;
    semantic::OwnershipKind ownership = semantic::OwnershipKind::unspecified;
    semantic::LifetimeKind lifetime = semantic::LifetimeKind::unknown;
    semantic::Mutability mutability = semantic::Mutability::immutable;
    AbiFunctionId release{};
    bool matched_abi_contract = false;
};

struct LoweredTemplateInstance {
    semantic::TemplateInstanceId instance{};
    Hash128 result{};
    AbiTypeId abi_type{};
    AbiFunctionId abi_function{};
    LoweringStatus status = LoweringStatus::rejected;
    LoweringReason reason = LoweringReason::template_not_materialized;
};

struct LoweringResult {
    std::vector<LoweredType> types;
    std::vector<LoweredFunction> functions;
    std::vector<LoweredOwnership> ownership;
    std::vector<LoweredTemplateInstance> templates;
    uint32_t rules_version = 1;

    [[nodiscard]] bool has_rejections() const {
        for (const auto &type : types)
            if (type.status == LoweringStatus::rejected) return true;
        for (const auto &function : functions)
            if (function.status == LoweringStatus::rejected) return true;
        for (const auto &instance : templates)
            if (instance.status == LoweringStatus::rejected) return true;
        return false;
    }
};

}   // namespace amc::ir

#endif   // AMC_IR_LOWERING_H
