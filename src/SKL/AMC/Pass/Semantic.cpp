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
#include "SKL/AMC/Pass/Semantic.h"

#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

namespace amc::pass {
namespace {

struct IdKey {
    uint64_t lo;
    uint64_t hi;
    bool operator==(const IdKey &other) const noexcept { return lo == other.lo && hi == other.hi; }
};

struct IdKeyHash {
    size_t operator()(const IdKey &key) const noexcept {
        return static_cast<size_t>(key.lo ^ (key.hi * 0x9e3779b97f4a7c15ULL));
    }
};

IdKey key(amc::Hash128 value) { return {value.lo, value.hi}; }

IdKey key(amc::semantic::SemanticTypeId value) { return key(value.value); }

IdKey key(amc::semantic::SemanticFunctionId value) { return key(value.value); }

bool add_error(diagnostics::DiagnosticList &diagnostics, std::string message) {
    diagnostics::add_error(diagnostics, "semantic_invalid", "pass", std::move(message));
    return false;
}

}   // namespace

bool verify_semantic(ir::Module &module, Context &context, diagnostics::DiagnosticList &diagnostics) {
    (void)context;
    std::unordered_set<IdKey, IdKeyHash> types;
    std::unordered_set<IdKey, IdKeyHash> functions;
    for (const auto &type : module.semantic.types) {
        if (!type.id.valid()) return add_error(diagnostics, "semantic type has no identity");
        if (!types.emplace(key(type.id)).second) return add_error(diagnostics, "duplicate semantic type identity");
    }
    for (const auto &function : module.semantic.functions) {
        if (!function.id.valid()) return add_error(diagnostics, "semantic function has no identity");
        if (!functions.emplace(key(function.id)).second)
            return add_error(diagnostics, "duplicate semantic function identity");
    }

    const auto require_type = [&](amc::semantic::SemanticTypeId id, const std::string &what) {
        if (id.valid() && types.count(key(id)) == 0) return add_error(diagnostics, "unknown semantic type: " + what);
        return true;
    };
    const auto require_function = [&](amc::semantic::SemanticFunctionId id, const std::string &what) {
        if (id.valid() && functions.count(key(id)) == 0)
            return add_error(diagnostics, "unknown semantic function: " + what);
        return true;
    };

    for (const auto &record : module.semantic.records) {
        if (!require_type(record.type, "record")) return false;
        for (const auto &base : record.bases)
            if (!require_type(base.type, "base class")) return false;
        for (const auto &field : record.fields)
            if (!require_type(field.type, "field " + field.name)) return false;
    }
    for (const auto &function : module.semantic.functions) {
        if (!require_type(function.owner, "function owner " + function.qualified_name)) return false;
        if (!require_type(function.return_type, "function return " + function.qualified_name)) return false;
        for (const auto &parameter : function.parameters)
            if (!require_type(parameter.type, "function parameter " + function.qualified_name)) return false;
    }
    for (const auto &ownership : module.semantic.ownership) {
        if (!require_function(ownership.function, "ownership function")) return false;
        if (!require_function(ownership.release, "ownership release")) return false;
    }
    for (const auto &exposed : module.semantic.exposed)
        if (!require_type(exposed.type, "exposed type " + exposed.qualified_name)) return false;
    return true;
}

}   // namespace amc::pass
