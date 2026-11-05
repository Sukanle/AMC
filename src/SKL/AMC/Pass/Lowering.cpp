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
#include <utility>
#include <vector>

namespace amc::pass {
namespace {

std::string normalize_name(std::string name) {
    while (name.size() >= 2 && name[0] == ':' && name[1] == ':') name.erase(0, 2);
    return name;
}

void report(diagnostics::DiagnosticList &diagnostics, const LoweringOptions &options, std::string code,
    std::string message) {
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
    for (uint32_t i = 0; i < module.abi.functions.size(); ++i)
        abi_functions[normalize_name(module.abi.functions[i].name)].emplace_back(&module.abi.functions[i], i);

    std::map<semantic::SemanticTypeId, const semantic::SemanticRecord *> records;
    for (const auto &record : module.semantic.records)
        records[record.type] = &record;

    std::map<semantic::SemanticTypeId, uint32_t> lowered_type_index;
    for (const auto &type : module.semantic.types) {
        ir::LoweredType lowered;
        lowered.semantic = type.id;
        lowered.name = type.qualified_name;
        if (const auto found = abi_types.find(normalize_name(type.qualified_name)); found != abi_types.end()) {
            lowered.abi = found->second.first->id;
            lowered.abi_index = found->second.second;
            lowered.size = found->second.first->size;
            lowered.align = found->second.first->align;
            lowered.layout_hash = found->second.first->layout_hash;
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

        std::vector<std::pair<const Function *, uint32_t>> candidates;
        const auto qualified = abi_functions.find(normalize_name(function.qualified_name));
        if (qualified != abi_functions.end()) candidates = qualified->second;
        if (candidates.empty()) {
            const auto plain = abi_functions.find(normalize_name(function.unqualified_name));
            if (plain != abi_functions.end()) candidates = plain->second;
        }
        std::vector<std::pair<const Function *, uint32_t>> matching;
        for (const auto &candidate : candidates)
            if (candidate.first->parameters.size() == function.parameters.size()) matching.push_back(candidate);

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
        if (const auto found = lowered_function_index.find(ownership.function); found != lowered_function_index.end())
            lowered.abi = result.functions[found->second].abi;
        else
            report(diagnostics, options, "lowering_ownership_unresolved",
                "ownership contract has no lowered function");
        result.ownership.push_back(std::move(lowered));
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

}   // namespace amc::pass
