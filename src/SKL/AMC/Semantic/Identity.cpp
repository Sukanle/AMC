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
#include "SKL/AMC/Semantic/Identity.h"

#include <string>

namespace amc::semantic {
namespace {

// Domain separators keep the four identity families independent even when
// their canonical keys happen to share bytes.
constexpr uint64_t semantic_type_domain = 0x53454d5459504531ULL;       // "SEMTYPE1"
constexpr uint64_t semantic_function_domain = 0x53454d46554e4331ULL;   // "SEMFUNC1"
constexpr uint64_t template_decl_domain = 0x54504c4445434c31ULL;       // "TPLDECL1"
constexpr uint64_t template_instance_domain = 0x54504c494e535431ULL;   // "TPLINST1"

void append_u32(std::string &out, uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xff));
    out.push_back(static_cast<char>((value >> 16) & 0xff));
    out.push_back(static_cast<char>((value >> 8) & 0xff));
    out.push_back(static_cast<char>(value & 0xff));
}

void append_u64(std::string &out, uint64_t value) {
    append_u32(out, static_cast<uint32_t>(value >> 32));
    append_u32(out, static_cast<uint32_t>(value & 0xffffffffULL));
}

void append_string(std::string &out, std::string_view value) {
    append_u32(out, static_cast<uint32_t>(value.size()));
    out.append(value.data(), value.size());
}

void append_hash(std::string &out, Hash128 value) {
    append_u64(out, value.lo);
    append_u64(out, value.hi);
}

void append_type_id(std::string &out, SemanticTypeId value) { append_hash(out, value.value); }

void append_function_id(std::string &out, SemanticFunctionId value) { append_hash(out, value.value); }

void append_template_decl_id(std::string &out, TemplateDeclId value) { append_hash(out, value.value); }

void append_template_instance_id(std::string &out, TemplateInstanceId value) { append_hash(out, value.value); }

void append_qualifiers(std::string &out, const Qualifiers &qualifiers) {
    append_u32(
        out, (qualifiers.is_const ? 1u : 0u) | (qualifiers.is_volatile ? 2u : 0u) | (qualifiers.is_restrict ? 4u : 0u));
}

}   // namespace

SemanticTypeId semantic_type_identity(SemanticTypeKind kind, std::string_view canonical_name,
    const Qualifiers &qualifiers, ValueCategory category, SemanticTypeId canonical, SemanticTypeId element,
    TemplateInstanceId instance, uint32_t rules_version) {
    std::string key;
    append_u32(key, rules_version);
    append_u32(key, static_cast<uint32_t>(kind));
    append_string(key, canonical_name);
    append_qualifiers(key, qualifiers);
    append_u32(key, static_cast<uint32_t>(category));
    append_type_id(key, canonical);
    append_type_id(key, element);
    append_template_instance_id(key, instance);
    return SemanticTypeId{hash_text(key, semantic_type_domain)};
}

SemanticFunctionId semantic_function_identity(SemanticFunctionKind kind, SemanticTypeId owner,
    std::string_view unqualified_name, const std::vector<SemanticTypeId> &parameters, const Qualifiers &qualifiers,
    ValueCategory ref_qualifier, TemplateInstanceId instance, uint32_t rules_version) {
    std::string key;
    append_u32(key, rules_version);
    append_u32(key, static_cast<uint32_t>(kind));
    append_type_id(key, owner);
    append_string(key, unqualified_name);
    append_u32(key, static_cast<uint32_t>(parameters.size()));
    for (const SemanticTypeId parameter : parameters)
        append_type_id(key, parameter);
    append_qualifiers(key, qualifiers);
    append_u32(key, static_cast<uint32_t>(ref_qualifier));
    append_template_instance_id(key, instance);
    return SemanticFunctionId{hash_text(key, semantic_function_domain)};
}

TemplateDeclId template_decl_identity(TemplateKind kind, std::string_view qualified_name,
    const std::vector<TemplateParameter> &parameters, Hash128 requires_hash, uint32_t rules_version) {
    std::string key;
    append_u32(key, rules_version);
    append_u32(key, static_cast<uint32_t>(kind));
    append_string(key, qualified_name);
    append_u32(key, static_cast<uint32_t>(parameters.size()));
    for (const TemplateParameter &parameter : parameters) {
        append_u32(key, static_cast<uint32_t>(parameter.kind));
        append_u32(key, parameter.index);
        append_string(key, parameter.name);
        append_hash(key, parameter.default_argument);
        append_hash(key, parameter.constraint);
    }
    append_hash(key, requires_hash);
    return TemplateDeclId{hash_text(key, template_decl_domain)};
}

TemplateInstanceId template_instance_identity(TemplateDeclId declaration,
    const std::vector<TemplateArgumentKey> &arguments, bool explicit_specialization, uint32_t rules_version) {
    std::string key;
    append_u32(key, rules_version);
    append_template_decl_id(key, declaration);
    append_u32(key, explicit_specialization ? 1u : 0u);
    append_u32(key, static_cast<uint32_t>(arguments.size()));
    for (const TemplateArgumentKey &argument : arguments) {
        append_u32(key, static_cast<uint32_t>(argument.kind));
        append_type_id(key, argument.type);
        append_u64(key, static_cast<uint64_t>(argument.value));
        append_template_decl_id(key, argument.template_decl);
        append_hash(key, argument.canonical);
    }
    return TemplateInstanceId{hash_text(key, template_instance_domain)};
}

}   // namespace amc::semantic
