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
#include "SKL/AMC/Semantic/Sync.h"

#include "SKL/AMC/Semantic/Identity.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace amc::semantic {
namespace {

SemanticTypeKind semantic_kind(TypeKind kind) {
    switch (kind) {
        case TypeKind::primitive:      return SemanticTypeKind::builtin;
        case TypeKind::enumeration:    return SemanticTypeKind::enumeration;
        case TypeKind::record:         return SemanticTypeKind::record;
        case TypeKind::pointer:        return SemanticTypeKind::pointer;
        case TypeKind::array:          return SemanticTypeKind::array;
        case TypeKind::function:       return SemanticTypeKind::function;
        case TypeKind::namespace_type: return SemanticTypeKind::unknown;
        case TypeKind::alias:          return SemanticTypeKind::unknown;
    }
    return SemanticTypeKind::unknown;
}

std::string unqualified(std::string_view name) {
    const size_t separator = name.rfind("::");
    return std::string(separator == std::string_view::npos ? name : name.substr(separator + 2));
}

Visibility visibility_of(uint32_t flags) {
    switch ((flags & field_visibility_mask) >> field_visibility_shift) {
        case 0:  return Visibility::public_;
        case 1:  return Visibility::protected_;
        case 2:  return Visibility::private_;
        default: return Visibility::none;
    }
}

struct TypeIds {
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, SemanticTypeId>> by_hash;
    std::unordered_map<std::string, SemanticTypeId> by_name;

    void add(const Hash128 &hash, SemanticTypeId id) { by_hash[hash.lo][hash.hi] = id; }

    SemanticTypeId find(const Hash128 &hash) const {
        const auto lo = by_hash.find(hash.lo);
        if (lo == by_hash.end()) return {};
        const auto hi = lo->second.find(hash.hi);
        return hi == lo->second.end() ? SemanticTypeId{} : hi->second;
    }
};

}   // namespace

void synchronize_from_abi(const AbiModule &module, SemanticModule &semantic) {
    if (semantic.rules_version == 0) semantic.rules_version = semantic_rules_version;
    TypeIds type_ids;
    for (const auto &type : semantic.types) {
        if (!type.id.valid()) continue;
        type_ids.by_name.emplace(type.qualified_name, type.id);
        if (type.abi_type.lo != 0 || type.abi_type.hi != 0) type_ids.add(type.abi_type, type.id);
    }

    // Every ABI type gets a semantic identity. Prefer an existing source
    // record with the same canonical name; otherwise synthesize one whose
    // identity depends only on the ABI type spelling and kind.
    for (const auto &type : module.types) {
        if (type.id.lo == 0 && type.id.hi == 0) continue;
        SemanticTypeId id = type_ids.find(type.id);
        if (!id.valid()) {
            const auto named = type_ids.by_name.find(type.name);
            if (named != type_ids.by_name.end()) id = named->second;
        }
        if (!id.valid()) {
            id = semantic_type_identity(semantic_kind(type.kind), type.name, Qualifiers{}, ValueCategory::value, {}, {},
                {}, semantic.rules_version);
            semantic.types.push_back(
                {id, semantic_kind(type.kind), type.name, Qualifiers{}, {}, {}, {}, type.id, {}, 0});
        } else {
            for (auto &existing : semantic.types)
                if (existing.id == id) {
                    existing.abi_type = type.id;
                    break;
                }
        }
        type_ids.add(type.id, id);
        type_ids.by_name.emplace(type.name, id);
    }

    // Records and their inheritance/field layout are ABI facts already extracted
    // by the frontend; mirror them so lowering can attach each field to an
    // AbiTypeId without looking the name up again.
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> record_keys;
    for (const auto &record : semantic.records)
        record_keys[record.type.value.lo].insert(record.type.value.hi);
    for (const auto &type : module.types) {
        if (type.kind != TypeKind::record && type.kind != TypeKind::alias) continue;
        const SemanticTypeId id = type_ids.find(type.id);
        if (!id.valid()) continue;
        if (record_keys[id.value.lo].count(id.value.hi) != 0) continue;

        SemanticRecord record;
        record.type = id;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = module.fields[type.field_begin + i];
            const SemanticTypeId field_type = type_ids.find(field.type_id);
            if (!field_type.valid()) continue;
            if (field.flags & field_base) {
                SemanticBase base;
                base.type = field_type;
                base.bit_offset = field.offset * 8;
                base.visibility = visibility_of(field.flags);
                record.bases.push_back(std::move(base));
                continue;
            }
            SemanticField out;
            out.name = field.name;
            out.type = field_type;
            out.bit_offset = field.offset * 8;
            out.visibility = visibility_of(field.flags);
            if (field.flags & field_bitfield) {
                out.bit_offset += (field.flags >> field_bit_offset_shift) & 0xFF;
                out.bit_width = (field.flags >> field_bit_width_shift) & 0xFF;
            }
            record.fields.push_back(std::move(out));
        }
        record_keys[id.value.lo].insert(id.value.hi);
        semantic.records.push_back(std::move(record));
    }

    // Every ABI function gets a source-level identity and a direct signature
    // link. Existing frontend records (for example a materialized template
    // specialization) are reused when their source identity is the same.
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> function_keys;
    for (const auto &function : semantic.functions)
        function_keys[function.id.value.lo].insert(function.id.value.hi);
    for (const auto &function : module.functions) {
        if (function.signature.lo == 0 && function.signature.hi == 0) continue;
        SemanticTypeId owner;
        if (function.owner_type.lo != 0 || function.owner_type.hi != 0) owner = type_ids.find(function.owner_type);
        const SemanticTypeId return_type = type_ids.find(function.return_type);
        std::vector<SemanticTypeId> parameters;
        parameters.reserve(function.parameters.size());
        bool complete = true;
        for (const auto &parameter : function.parameters) {
            const SemanticTypeId parameter_type = type_ids.find(parameter.type_id);
            if (!parameter_type.valid()) {
                complete = false;
                break;
            }
            parameters.push_back(parameter_type);
        }
        if (!complete) continue;

        const SemanticFunctionKind kind =
            owner.valid() ? SemanticFunctionKind::member_function : SemanticFunctionKind::free_function;
        const std::string name = unqualified(function.name);
        SemanticFunctionId id = semantic_function_identity(
            kind, owner, name, parameters, Qualifiers{}, ValueCategory::value, {}, semantic.rules_version);

        SemanticFunction *existing = nullptr;
        for (auto &candidate : semantic.functions) {
            if (candidate.id == id) {
                existing = &candidate;
                break;
            }
        }
        if (existing == nullptr) {
            // Constructors, destructors and conversion operators have a
            // different function kind in the source model than the generic
            // member-function identity above. Match the declaration by name and
            // arity when there is exactly one unlinked candidate.
            SemanticFunction *candidate = nullptr;
            size_t matches = 0;
            for (auto &item : semantic.functions) {
                if (item.qualified_name != function.name || item.parameters.size() != parameters.size()) continue;
                if (item.abi_signature.lo != 0 || item.abi_signature.hi != 0) continue;
                candidate = &item;
                ++matches;
            }
            if (matches == 1) existing = candidate;
        }
        if (existing != nullptr) {
            existing->abi_signature = function.signature;
            existing->owner = owner;
            existing->return_type = return_type;
            existing->parameters.clear();
            for (size_t i = 0; i < parameters.size(); ++i) {
                SemanticParameter parameter;
                parameter.name = function.parameters[i].name;
                parameter.type = parameters[i];
                existing->parameters.push_back(std::move(parameter));
            }
            continue;
        }
        if (function_keys[id.value.lo].count(id.value.hi) != 0) {
            for (auto &item : semantic.functions)
                if (item.id == id) item.abi_signature = function.signature;
            continue;
        }

        SemanticFunction record;
        record.id = id;
        record.kind = kind;
        record.qualified_name = function.name;
        record.unqualified_name = name;
        record.owner = owner;
        record.return_type = return_type;
        record.abi_signature = function.signature;
        for (size_t i = 0; i < parameters.size(); ++i) {
            SemanticParameter parameter;
            parameter.name = function.parameters[i].name;
            parameter.type = parameters[i];
            record.parameters.push_back(std::move(parameter));
        }
        function_keys[id.value.lo].insert(id.value.hi);
        semantic.functions.push_back(std::move(record));
    }
}

}   // namespace amc::semantic
