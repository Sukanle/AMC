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
#include "SKL/AMC/IR/Verify.h"

#include "SKL/AMC/Core/Reflect.h"
#include "SKL/AMC/IR/Reflect.h"
#include "SKL/AMC/Diagnostics/Error.h"

#include <map>
#include <utility>

#include <fmt/format.h>

namespace skl::amc {
namespace {

using TypeKey = std::pair<uint64_t, uint64_t>;

TypeKey key(Hash128 value) { return {value.lo, value.hi}; }

std::string hash_string(Hash128 value) {
    if (value == Hash128{}) return "0X0";
    return fmt::format("0X{:016x}{:016x}", value.hi, value.lo);
}

using TypeMap = std::map<TypeKey, const Type *>;
using FieldMap = std::map<TypeKey, std::vector<const Field *>>;
using FunctionMap = std::map<TypeKey, std::vector<const Function *>>;

TypeMap index_types(const ABIModule &module) {
    TypeMap map;
    for (const auto &type : module.types)
        map[key(type.id)] = &type;
    return map;
}

FieldMap index_fields(const ABIModule &module) {
    FieldMap map;
    for (const auto &field : module.fields)
        map[key(field.owner_type)].push_back(&field);
    return map;
}

FunctionMap index_functions(const ABIModule &module) {
    FunctionMap map;
    for (const auto &function : module.functions)
        map[key(function.owner_type)].push_back(&function);
    return map;
}

const Field *find_field(const std::vector<const Field *> &fields, const std::string &name) {
    for (const auto *field : fields)
        if (field->name == name) return field;
    return nullptr;
}

void add_change(VerifyResult &result, ChangeKind kind, std::string type, std::string name, std::string detail) {
    result.consistent = false;
    result.changes.push_back({kind, std::move(type), std::move(name), std::move(detail)});
}

std::string layout_detail(const Type &contract, const Type &implementation) {
    return fmt::format("layout_hash {} -> {}, size {} -> {}, align {} -> {}", hash_string(contract.layout_hash),
        hash_string(implementation.layout_hash), contract.size, implementation.size, contract.align,
        implementation.align);
}

void compare_type(const Type &contract, const Type &implementation, const FieldMap &contract_fields,
    const FieldMap &implementation_fields, VerifyResult &result) {
    if (!(contract.layout_hash == implementation.layout_hash)
        || contract.size != implementation.size
        || contract.align != implementation.align)
        add_change(result, ChangeKind::LAYOUT_CHANGED, contract.name, {}, layout_detail(contract, implementation));

    const auto &contract_owned =
        contract_fields.count(key(contract.id)) ? contract_fields.at(key(contract.id)) : std::vector<const Field *>{};
    const auto &implementation_owned = implementation_fields.count(key(contract.id))
                                         ? implementation_fields.at(key(contract.id))
                                         : std::vector<const Field *>{};

    for (const auto *field : contract_owned) {
        const Field *match = find_field(implementation_owned, field->name);
        if (!match) {
            add_change(result, ChangeKind::FIELD_REMOVED, contract.name, field->name, "field removed");
            continue;
        }
        if (match->offset != field->offset)
            add_change(result, ChangeKind::FIELD_OFFSET_CHANGED, contract.name, field->name,
                fmt::format("offset {} -> {}", field->offset, match->offset));
        if (!(match->type_id == field->type_id))
            add_change(result, ChangeKind::FIELD_TYPE_CHANGED, contract.name, field->name,
                fmt::format("type {} -> {}", hash_string(field->type_id), hash_string(match->type_id)));
    }
    for (const auto *field : implementation_owned)
        if (!find_field(contract_owned, field->name))
            add_change(result, ChangeKind::FIELD_ADDED, contract.name, field->name, "field added");
}

void compare_function(const Function &contract, const Function &match, VerifyResult &result) {
    if (contract.calling_convention != match.calling_convention) {
        add_change(result, ChangeKind::CALLING_CONVENTION_CHANGED, match.name, match.name,
            fmt::format("calling convention {} -> {}", contract.calling_convention, match.calling_convention));
    }
    if (!(contract.signature == match.signature)) {
        add_change(result, ChangeKind::FUNCTION_SIGNATURE_CHANGED,
            match.owner_type == Hash128{} ? std::string{} : match.name, match.name,
            fmt::format("signature {} -> {}", hash_string(contract.signature), hash_string(match.signature)));
        return;
    }
    if (contract.params.size() != match.params.size()) {
        add_change(result, ChangeKind::FUNCTION_PARAMETER_CHANGED, {}, match.name,
            fmt::format("parameter count {} -> {}", contract.params.size(), match.params.size()));
        return;
    }
    for (size_t i = 0; i < contract.params.size(); ++i) {
        if (!(contract.params[i].type_id == match.params[i].type_id)) {
            add_change(result, ChangeKind::FUNCTION_PARAMETER_CHANGED, {}, match.name,
                fmt::format("parameter {} type {} -> {}", i, hash_string(contract.params[i].type_id),
                    hash_string(match.params[i].type_id)));
            return;
        }
    }
}

std::string function_key(const Function &function) {
    return function.name + "#" + hash_string(function.signature) + "#" + hash_string(function.owner_type);
}

const char *ownership_mode_name(OwnershipMode mode) { return reflect::enum_name(mode, "unknown"); }

const char *status_source_name(StatusSource source) { return reflect::enum_name(source, "unknown"); }

struct InteractionEntry {
    std::string function;
    const OwnershipContract *contract = nullptr;
};

bool ownership_key(
    const ABIModule &module, const OwnershipContract &contract, std::string &key, std::string &function) {
    if (contract.target == OwnershipTarget::RETURN) {
        if (contract.target_index >= module.functions.size()) return false;
        function = module.functions[contract.target_index].name;
        key = function_key(module.functions[contract.target_index]) + ":return";
        return true;
    }
    uint32_t base = 0;
    for (const auto &candidate : module.functions) {
        const uint32_t count = static_cast<uint32_t>(candidate.params.size());
        if (contract.target_index < base + count) {
            function = candidate.name;
            key = function_key(candidate) + ":param:" + std::to_string(contract.target_index - base);
            return true;
        }
        base += count;
    }
    return false;
}

std::map<std::string, InteractionEntry> index_ownership(const ABIModule &module) {
    std::map<std::string, InteractionEntry> entries;
    for (const auto &contract : module.contracts) {
        std::string key;
        std::string function;
        if (!ownership_key(module, contract, key, function)) continue;
        entries[key] = InteractionEntry{std::move(function), &contract};
    }
    return entries;
}

struct ErrorEntry {
    std::string function;
    const ErrorContract *contract = nullptr;
};

std::map<std::string, ErrorEntry> index_errors(const ABIModule &module) {
    std::map<std::string, ErrorEntry> entries;
    for (const auto &contract : module.errors) {
        if (contract.function_index >= module.functions.size()) continue;
        const auto &function = module.functions[contract.function_index];
        entries[function_key(function) + ":error"] = ErrorEntry{function.name, &contract};
    }
    return entries;
}

void compare_contracts(const ABIModule &contract, const ABIModule &implementation, VerifyResult &result) {
    const auto contract_ownership = index_ownership(contract);
    const auto implementation_ownership = index_ownership(implementation);
    for (const auto &[key, entry] : contract_ownership) {
        const auto found = implementation_ownership.find(key);
        if (found == implementation_ownership.end()) {
            add_change(result, ChangeKind::OWNERSHIP_CHANGED, {}, entry.function, "ownership contract removed");
            continue;
        }
        const auto &before = *entry.contract;
        const auto &after = *found->second.contract;
        if (before.mode != after.mode || !(before.release == after.release) || before.flags != after.flags) {
            add_change(result, ChangeKind::OWNERSHIP_CHANGED, {}, entry.function,
                fmt::format("mode {} -> {}, release {} -> {}", ownership_mode_name(before.mode),
                    ownership_mode_name(after.mode), hash_string(before.release), hash_string(after.release)));
        }
    }
    for (const auto &[key, entry] : implementation_ownership)
        if (contract_ownership.find(key) == contract_ownership.end())
            add_change(result, ChangeKind::OWNERSHIP_CHANGED, {}, entry.function, "ownership contract added");

    const auto contract_errors = index_errors(contract);
    const auto implementation_errors = index_errors(implementation);
    for (const auto &[key, entry] : contract_errors) {
        const auto found = implementation_errors.find(key);
        if (found == implementation_errors.end()) {
            add_change(result, ChangeKind::ERROR_BOUNDARY_CHANGED, {}, entry.function, "error boundary removed");
            continue;
        }
        const auto &before = *entry.contract;
        const auto &after = *found->second.contract;
        if (before.status_source != after.status_source
            || before.status_parameter != after.status_parameter
            || before.success_value != after.success_value
            || before.value_parameter != after.value_parameter
            || before.domain != after.domain
            || before.condition != after.condition
            || before.flags != after.flags) {
            add_change(result, ChangeKind::ERROR_BOUNDARY_CHANGED, {}, entry.function,
                fmt::format("status {} -> {}, success {} -> {}, value parameter {} -> {}, domain {:#x} -> {:#x}, "
                            "condition {} -> {}",
                    status_source_name(before.status_source), status_source_name(after.status_source),
                    before.success_value, after.success_value, before.value_parameter, after.value_parameter,
                    before.domain, after.domain,
                    generic_condition_name(static_cast<GenericCondition>(before.condition)),
                    generic_condition_name(static_cast<GenericCondition>(after.condition))));
        }
    }
    for (const auto &[key, entry] : implementation_errors)
        if (contract_errors.find(key) == contract_errors.end())
            add_change(result, ChangeKind::ERROR_BOUNDARY_CHANGED, {}, entry.function, "error boundary added");
}

}   // namespace

const char *change_kind_name(ChangeKind kind) { return reflect::enum_name(kind, "unknown"); }

std::map<TypeKey, const InterfaceRecord *> index_interfaces(const ABIModule &module) {
    std::map<TypeKey, const InterfaceRecord *> entries;
    for (const auto &record : module.interfaces)
        entries.emplace(key(record.id), &record);
    return entries;
}

const InterfaceMethod *find_interface_method(const ABIModule &module, const InterfaceRecord &record, uint32_t slot) {
    for (uint32_t i = 0; i < record.method_count; ++i) {
        const auto &method = module.interface_methods[record.method_begin + i];
        if (method.slot == slot) return &method;
    }
    return nullptr;
}

void compare_interfaces(const ABIModule &contract, const ABIModule &implementation, VerifyResult &result) {
    const auto contract_interfaces = index_interfaces(contract);
    const auto implementation_interfaces = index_interfaces(implementation);
    for (const auto &[id, record] : contract_interfaces) {
        const auto found = implementation_interfaces.find(id);
        if (found == implementation_interfaces.end()) {
            add_change(
                result, ChangeKind::INTERFACE_CHANGED, record->name, {}, "interface missing from implementation");
            continue;
        }
        if (record->version != found->second->version) {
            add_change(result, ChangeKind::INTERFACE_CHANGED, record->name, {},
                fmt::format("version {} -> {}", record->version, found->second->version));
        }
        if (record->method_count != found->second->method_count) {
            add_change(result, ChangeKind::INTERFACE_CHANGED, record->name, {},
                fmt::format("method count {} -> {}", record->method_count, found->second->method_count));
            continue;
        }
        for (uint32_t i = 0; i < record->method_count; ++i) {
            const auto &before = contract.interface_methods[record->method_begin + i];
            const auto *after = find_interface_method(implementation, *found->second, before.slot);
            if (after == nullptr || !(before.signature == after->signature)) {
                add_change(result, ChangeKind::INTERFACE_CHANGED, record->name, before.name,
                    after == nullptr ? "method slot missing" : "method signature changed");
            }
        }
    }
    for (const auto &[id, record] : implementation_interfaces)
        if (!contract_interfaces.count(id))
            add_change(result, ChangeKind::INTERFACE_CHANGED, record->name, {}, "interface added to implementation");
}

bool verify_modules(
    const ABIModule &contract, const ABIModule &implementation, VerifyResult &result, std::string &error) {
    (void)error;
    result = VerifyResult{};
    result.package = contract.package_name;
    result.consistent = true;

    const TypeMap contract_types = index_types(contract);
    const TypeMap implementation_types = index_types(implementation);
    const FieldMap contract_fields = index_fields(contract);
    const FieldMap implementation_fields = index_fields(implementation);
    const FunctionMap contract_functions = index_functions(contract);
    const FunctionMap implementation_functions = index_functions(implementation);

    for (const auto &type : contract.types) {
        const auto it = implementation_types.find(key(type.id));
        if (it == implementation_types.end()) {
            add_change(result, ChangeKind::TYPE_REMOVED, type.name, {}, "type missing from implementation");
            continue;
        }
        compare_type(type, *it->second, contract_fields, implementation_fields, result);
    }
    for (const auto &type : implementation.types)
        if (!contract_types.count(key(type.id)))
            add_change(result, ChangeKind::TYPE_ADDED, type.name, {}, "type not present in contract");

    const auto compare_function_set = [&](const std::vector<const Function *> &contracts,
                                          const std::vector<const Function *> &matches, const std::string &owner_name) {
        // Functions belonging to one owner are compared by name. Index the
        // contract side once so a free-function-heavy module does not degrade
        // to O(n²) inside this set comparison.
        std::map<std::string, const Function *> contract_by_name;
        std::map<std::string, const Function *> implementation_by_name;
        for (const auto *contract_function : contracts)
            contract_by_name.emplace(contract_function->name, contract_function);
        for (const auto *implementation_function : matches)
            implementation_by_name.emplace(implementation_function->name, implementation_function);

        for (const auto *contract_function : contracts) {
            const auto match = implementation_by_name.find(contract_function->name);
            if (match == implementation_by_name.end()) {
                add_change(
                    result, ChangeKind::FUNCTION_REMOVED, owner_name, contract_function->name, "function removed");
                continue;
            }
            compare_function(*contract_function, *match->second, result);
        }
        for (const auto *implementation_function : matches)
            if (contract_by_name.find(implementation_function->name) == contract_by_name.end())
                add_change(
                    result, ChangeKind::FUNCTION_ADDED, owner_name, implementation_function->name, "function added");
    };

    // Compare every owner that appears in either module.
    std::map<TypeKey, std::string> owner_names;
    for (const auto &entry : contract_types)
        owner_names[entry.first] = entry.second->name;
    for (const auto &entry : implementation_types)
        owner_names.emplace(entry.first, entry.second->name);
    owner_names[key(Hash128{})] = "";

    for (const auto &entry : owner_names) {
        const auto contract_it = contract_functions.find(entry.first);
        const auto implementation_it = implementation_functions.find(entry.first);
        const std::vector<const Function *> empty;
        compare_function_set(contract_it == contract_functions.end() ? empty : contract_it->second,
            implementation_it == implementation_functions.end() ? empty : implementation_it->second, entry.second);
    }

    compare_contracts(contract, implementation, result);
    compare_interfaces(contract, implementation, result);

    return true;
}

std::string verify_result_to_text(const VerifyResult &result) {
    std::string out =
        fmt::format("verify: package={} status={}\n", result.package, result.consistent ? "consistent" : "drift");
    if (!result.contract_path.empty()) out += fmt::format("  contract: {}\n", result.contract_path);
    if (!result.implementation_path.empty()) out += fmt::format("  implementation: {}\n", result.implementation_path);
    for (const auto &change : result.changes) {
        out += fmt::format("  - {}", change_kind_name(change.kind));
        if (!change.type.empty()) out += fmt::format(" type={}", change.type);
        if (!change.name.empty()) out += fmt::format(" name={}", change.name);
        if (!change.detail.empty()) out += fmt::format(" ({})", change.detail);
        out += "\n";
    }
    return out;
}

std::string verify_result_to_json(const VerifyResult &result) {
    std::string out = fmt::format(
        "{{\"package\":\"{}\",\"contract\":\"{}\",\"implementation\":\"{}\",\"status\":\"{}\",\"changes\":[",
        json_escape(result.package), json_escape(result.contract_path), json_escape(result.implementation_path),
        result.consistent ? "consistent" : "drift");
    for (size_t i = 0; i < result.changes.size(); ++i) {
        const auto &change = result.changes[i];
        if (i) out += ",";
        out += fmt::format("{{\"kind\":\"{}\",\"type\":\"{}\",\"name\":\"{}\",\"detail\":\"{}\"}}",
            change_kind_name(change.kind), json_escape(change.type), json_escape(change.name),
            json_escape(change.detail));
    }
    out += "]}";
    return out;
}

std::string verify_report_to_text(const std::vector<VerifyResult> &results) {
    std::string out;
    for (const auto &result : results)
        out += verify_result_to_text(result);
    return out;
}

std::string verify_report_to_json(const std::vector<VerifyResult> &results, bool consistent) {
    std::string out =
        fmt::format("{{\"schema\":\"abix.verify/1\",\"consistent\":{},\"modules\":[", consistent ? "true" : "false");
    for (size_t i = 0; i < results.size(); ++i) {
        if (i) out += ",";
        out += verify_result_to_json(results[i]);
    }
    out += "]}\n";
    return out;
}

}   // namespace skl::amc
