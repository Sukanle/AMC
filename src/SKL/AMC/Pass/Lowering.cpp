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
#include "SKL/AMC/Pass/Lowering.h"

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace amc::pass {
namespace {

struct Hash128Hash {
    size_t operator()(Hash128 value) const noexcept {
        return static_cast<size_t>(value.lo ^ (value.hi * 0x9e3779b97f4a7c15ULL));
    }
};

std::string normalize_name(std::string name) {
    while (name.size() >= 2 && name[0] == ':' && name[1] == ':')
        name.erase(0, 2);
    return name;
}

void report(
    diagnostics::DiagnosticList &diagnostics, const LoweringOptions &options, std::string code, std::string message) {
    if (options.strict)
        diagnostics::add_error(diagnostics, std::move(code), "lowering", std::move(message));
    else
        diagnostics::add_warning(diagnostics, std::move(code), "lowering", std::move(message));
}

}   // namespace

bool lower(const ir::Module &module, ir::LoweringResult &result, diagnostics::DiagnosticList &diagnostics,
    const LoweringOptions &options) {
    result = {};
    result.rules_version = 1;

    std::map<std::string, std::pair<const Type *, uint32_t>> abi_types;
    for (uint32_t i = 0; i < module.abi.types.size(); ++i)
        abi_types.emplace(normalize_name(module.abi.types[i].name), std::make_pair(&module.abi.types[i], i));

    std::map<std::string, std::vector<std::pair<const Function *, uint32_t>>> abi_functions;
    std::unordered_map<Hash128, uint32_t, Hash128Hash> abi_type_by_id;
    std::unordered_map<Hash128, std::vector<uint32_t>, Hash128Hash> abi_function_by_signature;
    abi_type_by_id.reserve(module.abi.types.size());
    abi_function_by_signature.reserve(module.abi.functions.size());
    for (uint32_t i = 0; i < module.abi.types.size(); ++i)
        abi_type_by_id.emplace(module.abi.types[i].id, i);
    for (uint32_t i = 0; i < module.abi.functions.size(); ++i) {
        abi_functions[normalize_name(module.abi.functions[i].name)].emplace_back(&module.abi.functions[i], i);
        abi_function_by_signature[module.abi.functions[i].signature].push_back(i);
    }

    std::map<semantic::SemanticTypeId, const semantic::SemanticRecord *> records;
    for (const auto &record : module.semantic.records)
        records[record.type] = &record;

    std::map<semantic::SemanticTypeId, uint32_t> lowered_type_index;
    for (const auto &type : module.semantic.types) {
        ir::LoweredType lowered;
        lowered.semantic = type.id;
        lowered.name = type.qualified_name;

        // The frontend records a direct `abi_type` link. Prefer it so lowering
        // never has to re-identify a type by its spelling; the name lookup is a
        // compatibility fallback for older semantic sidecars.
        const Type *abi_type = nullptr;
        uint32_t abi_index = UINT32_MAX;
        if (type.abi_type.lo != 0 || type.abi_type.hi != 0) {
            const auto found = abi_type_by_id.find(type.abi_type);
            if (found != abi_type_by_id.end()) {
                abi_index = found->second;
                abi_type = &module.abi.types[abi_index];
            }
        }
        if (abi_type == nullptr) {
            if (const auto found = abi_types.find(normalize_name(type.qualified_name)); found != abi_types.end()) {
                abi_type = found->second.first;
                abi_index = found->second.second;
            }
        }
        if (abi_type != nullptr) {
            lowered.abi = abi_type->id;
            lowered.abi_index = abi_index;
            lowered.size = abi_type->size;
            lowered.align = abi_type->align;
            lowered.layout_hash = abi_type->layout_hash;
            lowered.status = ir::LoweringStatus::lowered;
            lowered.reason = ir::LoweringReason::ok;
        } else {
            lowered.status = ir::LoweringStatus::rejected;
            lowered.reason = ir::LoweringReason::missing_abi_type;
            report(diagnostics, options, "lowering_missing_abi_type",
                "no ABI type for semantic type: " + type.qualified_name);
        }
        lowered_type_index[type.id] = static_cast<uint32_t>(result.types.size());
        result.types.push_back(std::move(lowered));
    }

    for (auto &lowered : result.types) {
        const auto record = records.find(lowered.semantic);
        if (record == records.end()) continue;
        for (const auto &base : record->second->bases) {
            ir::LoweredBase item;
            item.semantic = base.type;
            item.bit_offset = base.bit_offset;
            item.is_virtual = base.is_virtual;
            if (const auto found = lowered_type_index.find(base.type); found != lowered_type_index.end())
                item.abi = result.types[found->second].abi;
            else
                lowered.reason = ir::LoweringReason::layout_unverified;
            lowered.bases.push_back(item);
        }
        for (const auto &field : record->second->fields) {
            ir::LoweredField item;
            item.name = field.name;
            item.bit_offset = field.bit_offset;
            item.bit_width = field.bit_width;
            if (const auto found = lowered_type_index.find(field.type); found != lowered_type_index.end())
                item.type = result.types[found->second].abi;
            else
                lowered.reason = ir::LoweringReason::layout_unverified;
            lowered.fields.push_back(std::move(item));
        }
    }

    std::map<semantic::SemanticFunctionId, uint32_t> lowered_function_index;
    for (const auto &function : module.semantic.functions) {
        ir::LoweredFunction lowered;
        lowered.semantic = function.id;
        lowered.name = function.qualified_name;

        // The frontend records the ABI signature produced for this overload;
        // that is the authoritative link. Name + arity remains only for
        // semantic sidecars written before the direct link existed.
        std::vector<std::pair<const Function *, uint32_t>> matching;
        if (function.abi_signature.lo != 0 || function.abi_signature.hi != 0) {
            const auto found = abi_function_by_signature.find(function.abi_signature);
            if (found != abi_function_by_signature.end())
                for (const uint32_t index : found->second)
                    matching.emplace_back(&module.abi.functions[index], index);
        } else {
            std::vector<std::pair<const Function *, uint32_t>> candidates;
            const auto qualified = abi_functions.find(normalize_name(function.qualified_name));
            if (qualified != abi_functions.end()) candidates = qualified->second;
            if (candidates.empty()) {
                const auto plain = abi_functions.find(normalize_name(function.unqualified_name));
                if (plain != abi_functions.end()) candidates = plain->second;
            }
            for (const auto &candidate : candidates)
                if (candidate.first->parameters.size() == function.parameters.size()) matching.push_back(candidate);
        }

        if (matching.size() == 1) {
            lowered.abi = matching.front().first->signature;
            lowered.signature = matching.front().first->signature;
            lowered.return_type = matching.front().first->return_type;
            lowered.abi_index = matching.front().second;
            for (const auto &parameter : matching.front().first->parameters)
                lowered.parameters.push_back({parameter.type_id, parameter.flags});
            lowered.status = ir::LoweringStatus::lowered;
            lowered.reason = ir::LoweringReason::ok;
        } else if (matching.empty()) {
            lowered.status = ir::LoweringStatus::rejected;
            lowered.reason = ir::LoweringReason::missing_abi_function;
            report(diagnostics, options, "lowering_missing_abi_function",
                "no ABI function for semantic function: " + function.qualified_name);
        } else {
            lowered.status = ir::LoweringStatus::rejected;
            lowered.reason = ir::LoweringReason::ambiguous_abi_function;
            report(diagnostics, options, "lowering_ambiguous_abi_function",
                "ambiguous ABI function for semantic function: " + function.qualified_name);
        }
        lowered_function_index[function.id] = static_cast<uint32_t>(result.functions.size());
        result.functions.push_back(std::move(lowered));
    }

    for (const auto &ownership : module.semantic.ownership) {
        ir::LoweredOwnership lowered;
        lowered.function = ownership.function;
        lowered.interaction = ownership.interaction;
        lowered.index = ownership.index;
        lowered.ownership = ownership.ownership;
        lowered.lifetime = ownership.lifetime;
        lowered.mutability = ownership.mutability;
        if (const auto found = lowered_function_index.find(ownership.function); found != lowered_function_index.end()) {
            lowered.abi = result.functions[found->second].abi;
            if (const auto release = lowered_function_index.find(ownership.release);
                release != lowered_function_index.end())
                lowered.release = result.functions[release->second].abi;
            // Confirm the ownership declaration against the ABI contract that
            // was resolved from configuration. The backend consumes this flag
            // instead of comparing names.
            if (lowered.abi.lo != 0 || lowered.abi.hi != 0) {
                for (uint32_t i = 0; i < module.abi.functions.size(); ++i) {
                    if (!(module.abi.functions[i].signature == lowered.abi)) continue;
                    uint32_t parameter_base = 0;
                    for (uint32_t j = 0; j < i; ++j)
                        parameter_base += static_cast<uint32_t>(module.abi.functions[j].parameters.size());
                    const uint32_t target = lowered.interaction == semantic::InteractionKind::return_value
                                              ? i
                                              : parameter_base + lowered.index;
                    for (const auto &contract : module.abi.contracts)
                        if (static_cast<uint32_t>(contract.target) == static_cast<uint32_t>(lowered.interaction)
                            && contract.target_index == target) {
                            lowered.matched_abi_contract = true;
                            break;
                        }
                    break;
                }
            }
        } else {
            report(diagnostics, options, "lowering_ownership_unresolved", "ownership contract has no lowered function");
        }
        result.ownership.push_back(std::move(lowered));
    }

    for (const auto &exposed : module.semantic.exposed) {
        ir::LoweredExposedType lowered;
        lowered.semantic = exposed.type;
        lowered.name = exposed.qualified_name;
        lowered.symbol = exposed.symbol;
        lowered.has_constructor = exposed.has_constructor;
        lowered.opaque = exposed.opaque;

        const Type *abi_type = nullptr;
        if (exposed.abi_type.lo != 0 || exposed.abi_type.hi != 0)
            for (const auto &type : module.abi.types)
                if (type.id == exposed.abi_type) {
                    abi_type = &type;
                    break;
                }
        if (abi_type == nullptr)
            if (const auto found = abi_types.find(normalize_name(exposed.qualified_name)); found != abi_types.end())
                abi_type = found->second.first;

        if (abi_type == nullptr) {
            lowered.status = ir::LoweringStatus::rejected;
            lowered.reason = ir::LoweringReason::missing_abi_type;
            report(diagnostics, options, "lowering_exposed_missing_type",
                "no ABI type for exposed capability: " + exposed.qualified_name);
            result.exposed.push_back(std::move(lowered));
            continue;
        }

        lowered.abi = abi_type->id;
        lowered.status = ir::LoweringStatus::lowered;
        lowered.reason = ir::LoweringReason::ok;

        const auto find_function = [&](Hash128 signature) -> const Function * {
            if (signature.lo == 0 && signature.hi == 0) return nullptr;
            for (const auto &function : module.abi.functions)
                if (function.signature == signature) return &function;
            return nullptr;
        };
        if (const Function *create = find_function(exposed.create); create != nullptr)
            lowered.create = create->signature;
        else if (exposed.has_constructor) {
            lowered.status = ir::LoweringStatus::rejected;
            lowered.reason = ir::LoweringReason::missing_abi_function;
            report(diagnostics, options, "lowering_exposed_missing_constructor",
                "exposed capability has no create thunk: " + exposed.qualified_name);
        }
        if (const Function *destroy = find_function(exposed.destroy); destroy != nullptr)
            lowered.destroy = destroy->signature;

        for (const auto &method : exposed.methods) {
            if (find_function(method.abi_signature) == nullptr) {
                lowered.status = ir::LoweringStatus::rejected;
                lowered.reason = ir::LoweringReason::missing_abi_function;
                report(diagnostics, options, "lowering_exposed_missing_method",
                    "exposed capability has no thunk for method: " + exposed.qualified_name + "::" + method.name);
                continue;
            }
            ir::LoweredExposedMethod lowered_method;
            lowered_method.name = method.name;
            lowered_method.symbol = method.symbol;
            lowered_method.abi = method.abi_signature;
            lowered.methods.push_back(std::move(lowered_method));
        }
        result.exposed.push_back(std::move(lowered));
    }

    for (const auto &instance : module.semantic.instances) {
        ir::LoweredTemplateInstance lowered;
        lowered.instance = instance.id;
        lowered.result = instance.result;
        bool found = false;
        for (const auto &type : module.abi.types) {
            if (!(type.id == instance.result)) continue;
            lowered.abi_type = type.id;
            lowered.status = ir::LoweringStatus::lowered;
            lowered.reason = ir::LoweringReason::ok;
            found = true;
            break;
        }
        if (!found) {
            for (const auto &function : module.abi.functions) {
                if (!(function.signature == instance.result)) continue;
                lowered.abi_function = function.signature;
                lowered.status = ir::LoweringStatus::lowered;
                lowered.reason = ir::LoweringReason::ok;
                found = true;
                break;
            }
        }
        if (!found) {
            lowered.status = ir::LoweringStatus::rejected;
            lowered.reason = ir::LoweringReason::template_not_materialized;
            report(diagnostics, options, "lowering_template_not_materialized",
                "template instance has no materialized ABI entity");
        }
        result.templates.push_back(std::move(lowered));
    }

    return !diagnostics::has_errors(diagnostics);
}

bool lower_pass(ir::Module &module, diagnostics::DiagnosticList &diagnostics) {
    return lower(module, module.lowering, diagnostics, {});
}

}   // namespace amc::pass
