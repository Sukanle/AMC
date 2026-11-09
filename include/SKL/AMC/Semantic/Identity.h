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
#ifndef AMC_SEMANTIC_IDENTITY_H
#define AMC_SEMANTIC_IDENTITY_H

#include "SKL/AMC/Semantic/Model.h"

#include <stdint.h>

#include <string_view>
#include <vector>

namespace skl::amc::semantic {

// Bump when the canonical key encoding changes in a way that should produce
// different identities for the same source entity.
inline constexpr uint32_t semantic_rules_version = 1;

// SemanticTypeId covers the source-level type, including cv-qualifiers and the
// value category. ABI lowering computes a separate AbiTypeId and never
// overwrites these facts.
SemanticTypeId semantic_type_identity(SemanticTypeKind kind, std::string_view canonical_name,
    const Qualifiers &qualifiers, ValueCategory category, SemanticTypeId canonical, SemanticTypeId element,
    TemplateInstId instance, uint32_t rules_version = semantic_rules_version);

// SemanticFunctionId answers "which source overload is this?" It deliberately
// excludes the return type (C++ does not use it for ordinary overloads) and
// includes cv/ref qualifiers and template arguments.
SemanticFuncId semantic_function_identity(SemanticFuncKind kind, SemanticTypeId owner,
    std::string_view unqualified_name, const std::vector<SemanticTypeId> &parameters, const Qualifiers &qualifiers,
    ValueCategory ref_qualifier, TemplateInstId instance, uint32_t rules_version = semantic_rules_version);

TemplateDeclId template_decl_identity(TemplateTypeKind kind, std::string_view qualified_name,
    const std::vector<TemplateParam> &parameters, Hash128 requires_hash,
    uint32_t rules_version = semantic_rules_version);

struct TemplateArgKey {
    TemplateArgKind kind = TemplateArgKind::TYPE;
    SemanticTypeId type{};
    int64_t value = 0;
    TemplateDeclId template_decl{};
    Hash128 canonical{};   // canonical key for packs / other arguments
};

TemplateInstId template_instance_identity(TemplateDeclId declaration, const std::vector<TemplateArgKey> &arguments,
    bool explicit_specialization, uint32_t rules_version = semantic_rules_version);

}   // namespace skl::amc::semantic

#endif   // AMC_SEMANTIC_IDENTITY_H
