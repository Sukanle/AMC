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
#include "SKL/AMC/IR/LoweringJson.h"

#include "SKL/AMC/IR/Reflect.h"
#include "SKL/AMC/Semantic/Reflect.h"

#include "SKL/AMC/Private/Support/File.h"
#include "SKL/AMC/Support/Json.h"

#include <cerrno>
#include <cstdlib>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>

namespace skl::amc::ir {
namespace {

constexpr std::string_view lowering_schema = "amc.lowering/1";

std::string id_hex(Hash128 value) { return fmt::format("{:016x}{:016x}", value.hi, value.lo); }

bool parse_hex_u64(std::string_view text, uint64_t &value) {
    if (text.empty()) return false;
    std::string owned(text);
    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(owned.c_str(), &end, 16);
    if (errno != 0 || end == owned.c_str() || *end != '\0') return false;
    value = static_cast<uint64_t>(parsed);
    return true;
}

Hash128 hash_from_hex(const std::string &text) {
    Hash128 value{};
    if (text.size() > 16) {
        uint64_t hi = 0;
        uint64_t lo = 0;
        if (parse_hex_u64(std::string_view(text).substr(0, text.size() - 16), hi)
            && parse_hex_u64(std::string_view(text).substr(text.size() - 16), lo))
            value = {lo, hi};
        return value;
    }
    uint64_t lo = 0;
    if (parse_hex_u64(text, lo)) value.lo = lo;
    return value;
}

const char *status_name(LoweringStatus status) { return skl::amc::reflect::enum_name(status, "rejected"); }

LoweringStatus parse_status(const std::string &name) {
    return skl::amc::reflect::enum_value<LoweringStatus>(name, LoweringStatus::REJECTED);
}

const char *reason_name(LoweringReason reason) { return skl::amc::reflect::enum_name(reason, "missing_abi_type"); }

LoweringReason parse_reason(const std::string &name) {
    return skl::amc::reflect::enum_value<LoweringReason>(name, LoweringReason::MISSING_ABI_TYPE);
}

const char *interaction_name(semantic::InteractionKind kind) { return skl::amc::reflect::enum_name(kind, "parameter"); }

const char *ownership_name(semantic::OwnershipKind kind) { return skl::amc::reflect::enum_name(kind, "unspecified"); }

semantic::OwnershipKind parse_ownership(const std::string &name) {
    return skl::amc::reflect::enum_value<semantic::OwnershipKind>(name, semantic::OwnershipKind::UNSPECIFIED);
}

const char *lifetime_name(semantic::LifetimeKind kind) { return skl::amc::reflect::enum_name(kind, "unknown"); }

semantic::LifetimeKind parse_lifetime(const std::string &name) {
    return skl::amc::reflect::enum_value<semantic::LifetimeKind>(name, semantic::LifetimeKind::UNKNOWN);
}

const char *mutability_name(semantic::Mutability value) { return skl::amc::reflect::enum_name(value, "immutable"); }

semantic::Mutability parse_mutability(const std::string &name) {
    return skl::amc::reflect::enum_value<semantic::Mutability>(name, semantic::Mutability::IMMUTABLE);
}

const Json *field(const Json &object, const char *name) { return object.is_object() ? object.find(name) : nullptr; }

std::string string_field(const Json &object, const char *name) {
    const Json *node = field(object, name);
    return node != nullptr ? node->as_string() : std::string{};
}

long long number_field(const Json &object, const char *name) {
    const Json *node = field(object, name);
    return node != nullptr ? static_cast<long long>(node->as_number()) : 0;
}

bool bool_field(const Json &object, const char *name) {
    const Json *node = field(object, name);
    return node != nullptr && node->as_bool();
}

Hash128 hash_field(const Json &object, const char *name) {
    const Json *node = field(object, name);
    return node != nullptr ? hash_from_hex(node->as_string()) : Hash128{};
}

Json hash_json(Hash128 value) { return Json(id_hex(value)); }

bool valid(Hash128 value) { return value.lo != 0 || value.hi != 0; }

}   // namespace

bool write_lowering_json(const LoweringResult &result, const std::string &path, std::string &error) {
    Json root = Json::object();
    root.set("schema", std::string(lowering_schema));
    root.set("rules_version", static_cast<long long>(result.rules_version));

    Json types = Json::array();
    for (const auto &type : result.types) {
        Json value = Json::object();
        value.set("semantic", hash_json(type.semantic.value));
        value.set("abi", hash_json(type.abi));
        value.set("abi_index", static_cast<long long>(type.abi_index));
        value.set("name", type.name);
        value.set("status", status_name(type.status));
        value.set("reason", reason_name(type.reason));
        value.set("size", static_cast<long long>(type.size));
        value.set("align", static_cast<long long>(type.align));
        value.set("layout_hash", hash_json(type.layout_hash));
        Json bases = Json::array();
        for (const auto &base : type.bases) {
            Json item = Json::object();
            item.set("semantic", hash_json(base.semantic.value));
            item.set("abi", hash_json(base.abi));
            item.set("bit_offset", static_cast<long long>(base.bit_offset));
            item.set("virtual", base.is_virtual);
            bases.push_back(std::move(item));
        }
        value.set("bases", std::move(bases));
        Json fields = Json::array();
        for (const auto &item : type.fields) {
            Json field = Json::object();
            field.set("name", item.name);
            field.set("type", hash_json(item.type));
            field.set("bit_offset", static_cast<long long>(item.bit_offset));
            field.set("bit_width", static_cast<long long>(item.bit_width));
            fields.push_back(std::move(field));
        }
        value.set("fields", std::move(fields));
        types.push_back(std::move(value));
    }
    root.set("types", std::move(types));

    Json functions = Json::array();
    for (const auto &function : result.functions) {
        Json value = Json::object();
        value.set("semantic", hash_json(function.semantic.value));
        value.set("abi", hash_json(function.abi));
        value.set("abi_index", static_cast<long long>(function.abi_index));
        value.set("name", function.name);
        value.set("status", status_name(function.status));
        value.set("reason", reason_name(function.reason));
        value.set("signature", hash_json(function.signature));
        value.set("return_type", hash_json(function.return_type));
        Json parameters = Json::array();
        for (const auto &parameter : function.parameters) {
            Json item = Json::object();
            item.set("type", hash_json(parameter.type));
            item.set("flags", static_cast<long long>(parameter.flags));
            parameters.push_back(std::move(item));
        }
        value.set("parameters", std::move(parameters));
        functions.push_back(std::move(value));
    }
    root.set("functions", std::move(functions));

    Json ownership = Json::array();
    for (const auto &contract : result.ownership) {
        Json value = Json::object();
        value.set("function", hash_json(contract.function.value));
        value.set("abi", hash_json(contract.abi));
        value.set("interaction", interaction_name(contract.interaction));
        value.set("index", static_cast<long long>(contract.index));
        value.set("ownership", ownership_name(contract.ownership));
        value.set("lifetime", lifetime_name(contract.lifetime));
        value.set("mutability", mutability_name(contract.mutability));
        value.set("release", hash_json(contract.release));
        value.set("matched_abi_contract", contract.matched_abi_contract);
        ownership.push_back(std::move(value));
    }
    root.set("ownership", std::move(ownership));

    Json exposed = Json::array();
    for (const auto &record : result.exposed) {
        Json value = Json::object();
        value.set("semantic", hash_json(record.semantic.value));
        value.set("abi", hash_json(record.abi));
        value.set("name", record.name);
        value.set("symbol", record.symbol);
        value.set("status", status_name(record.status));
        value.set("reason", reason_name(record.reason));
        value.set("create", hash_json(record.create));
        value.set("destroy", hash_json(record.destroy));
        value.set("constructor", record.has_constructor);
        value.set("opaque", record.opaque);
        Json methods = Json::array();
        for (const auto &method : record.methods) {
            Json item = Json::object();
            item.set("name", method.name);
            item.set("symbol", method.symbol);
            item.set("abi", hash_json(method.abi));
            methods.push_back(std::move(item));
        }
        value.set("methods", std::move(methods));
        exposed.push_back(std::move(value));
    }
    root.set("exposed", std::move(exposed));

    Json templates = Json::array();
    for (const auto &instance : result.templates) {
        Json value = Json::object();
        value.set("instance", hash_json(instance.instance.value));
        value.set("result", hash_json(instance.result));
        value.set("abi_type", hash_json(instance.abi_type));
        value.set("abi_function", hash_json(instance.abi_function));
        value.set("status", status_name(instance.status));
        value.set("reason", reason_name(instance.reason));
        templates.push_back(std::move(value));
    }
    root.set("templates", std::move(templates));

    const std::string text = root.dump_pretty(2) + '\n';
    if (auto err = amc::support::write_text_file(path, text)) {
        error = "cannot write lowering sidecar: " + path + ": " + amc::support::error_message(std::move(err));
        return false;
    }
    return true;
}

bool read_lowering_json(const std::string &path, LoweringResult &result, std::string &error) {
    auto text_or = amc::support::read_text_file(path);
    if (!text_or) {
        error = "cannot read lowering sidecar: " + path + ": " + text_or.getError().message();
        return false;
    }
    const std::string text = std::move(*text_or);

    Json root;
    if (!parse_json(text, root, error)) {
        error = "invalid lowering sidecar " + path + ": " + error;
        return false;
    }
    if (!root.is_object()) {
        error = "invalid lowering sidecar " + path + ": root must be an object";
        return false;
    }
    const std::string schema = string_field(root, "schema");
    if (schema != std::string(lowering_schema)) {
        error = "unsupported lowering sidecar schema: " + schema;
        return false;
    }

    result = {};
    result.rules_version = static_cast<uint32_t>(number_field(root, "rules_version"));
    if (result.rules_version == 0) result.rules_version = 1;

    if (const Json *types = field(root, "types"); types != nullptr && types->is_array()) {
        for (const auto &value : types->items()) {
            LoweredType type;
            type.semantic = semantic::SemanticTypeId{hash_field(value, "semantic")};
            type.abi = hash_field(value, "abi");
            type.abi_index = static_cast<uint32_t>(number_field(value, "abi_index"));
            type.name = string_field(value, "name");
            type.status = parse_status(string_field(value, "status"));
            type.reason = parse_reason(string_field(value, "reason"));
            type.size = static_cast<uint32_t>(number_field(value, "size"));
            type.align = static_cast<uint32_t>(number_field(value, "align"));
            type.layout_hash = hash_field(value, "layout_hash");
            if (const Json *bases = field(value, "bases"); bases != nullptr && bases->is_array()) {
                for (const auto &item : bases->items()) {
                    LoweredBase base;
                    base.semantic = semantic::SemanticTypeId{hash_field(item, "semantic")};
                    base.abi = hash_field(item, "abi");
                    base.bit_offset = static_cast<uint32_t>(number_field(item, "bit_offset"));
                    base.is_virtual = bool_field(item, "virtual");
                    type.bases.push_back(std::move(base));
                }
            }
            if (const Json *fields = field(value, "fields"); fields != nullptr && fields->is_array()) {
                for (const auto &item : fields->items()) {
                    LoweredField lowered;
                    lowered.name = string_field(item, "name");
                    lowered.type = hash_field(item, "type");
                    lowered.bit_offset = static_cast<uint32_t>(number_field(item, "bit_offset"));
                    lowered.bit_width = static_cast<uint32_t>(number_field(item, "bit_width"));
                    type.fields.push_back(std::move(lowered));
                }
            }
            result.types.push_back(std::move(type));
        }
    }

    if (const Json *functions = field(root, "functions"); functions != nullptr && functions->is_array()) {
        for (const auto &value : functions->items()) {
            LoweredFunction function;
            function.semantic = semantic::SemanticFuncId{hash_field(value, "semantic")};
            function.abi = hash_field(value, "abi");
            function.abi_index = static_cast<uint32_t>(number_field(value, "abi_index"));
            function.name = string_field(value, "name");
            function.status = parse_status(string_field(value, "status"));
            function.reason = parse_reason(string_field(value, "reason"));
            function.signature = hash_field(value, "signature");
            function.return_type = hash_field(value, "return_type");
            if (const Json *parameters = field(value, "parameters"); parameters != nullptr && parameters->is_array()) {
                for (const auto &item : parameters->items()) {
                    LoweredParameter parameter;
                    parameter.type = hash_field(item, "type");
                    parameter.flags = static_cast<uint32_t>(number_field(item, "flags"));
                    function.parameters.push_back(parameter);
                }
            }
            result.functions.push_back(std::move(function));
        }
    }

    if (const Json *ownership = field(root, "ownership"); ownership != nullptr && ownership->is_array()) {
        for (const auto &value : ownership->items()) {
            LoweredOwnership contract;
            contract.function = semantic::SemanticFuncId{hash_field(value, "function")};
            contract.abi = hash_field(value, "abi");
            contract.interaction = string_field(value, "interaction") == "return_value"
                                     ? semantic::InteractionKind::RETURN
                                     : semantic::InteractionKind::PARAM;
            contract.index = static_cast<uint32_t>(number_field(value, "index"));
            contract.ownership = parse_ownership(string_field(value, "ownership"));
            contract.lifetime = parse_lifetime(string_field(value, "lifetime"));
            contract.mutability = parse_mutability(string_field(value, "mutability"));
            contract.release = hash_field(value, "release");
            contract.matched_abi_contract = bool_field(value, "matched_abi_contract");
            result.ownership.push_back(std::move(contract));
        }
    }

    if (const Json *exposed = field(root, "exposed"); exposed != nullptr && exposed->is_array()) {
        for (const auto &value : exposed->items()) {
            LoweredExposedType record;
            record.semantic = semantic::SemanticTypeId{hash_field(value, "semantic")};
            record.abi = hash_field(value, "abi");
            record.name = string_field(value, "name");
            record.symbol = string_field(value, "symbol");
            record.status = parse_status(string_field(value, "status"));
            record.reason = parse_reason(string_field(value, "reason"));
            record.create = hash_field(value, "create");
            record.destroy = hash_field(value, "destroy");
            record.has_constructor = bool_field(value, "constructor");
            record.opaque = bool_field(value, "opaque");
            if (const Json *methods = field(value, "methods"); methods != nullptr && methods->is_array()) {
                for (const auto &item : methods->items()) {
                    LoweredExposedMethod method;
                    method.name = string_field(item, "name");
                    method.symbol = string_field(item, "symbol");
                    method.abi = hash_field(item, "abi");
                    record.methods.push_back(std::move(method));
                }
            }
            result.exposed.push_back(std::move(record));
        }
    }

    if (const Json *templates = field(root, "templates"); templates != nullptr && templates->is_array()) {
        for (const auto &value : templates->items()) {
            LoweredTemplateInstance instance;
            instance.instance = semantic::TemplateInstId{hash_field(value, "instance")};
            instance.result = hash_field(value, "result");
            instance.abi_type = hash_field(value, "abi_type");
            instance.abi_function = hash_field(value, "abi_function");
            instance.status = parse_status(string_field(value, "status"));
            instance.reason = parse_reason(string_field(value, "reason"));
            result.templates.push_back(std::move(instance));
        }
    }

    return true;
}

bool load_lowering_sidecar(const std::string &artifact_path, LoweringResult &result, std::string &error) {
    const std::string path = artifact_path + ".lowering";
    if (!amc::support::path_exists(path)) {
        result = {};
        return true;
    }
    return read_lowering_json(path, result, error);
}

bool validate_lowering_references(const LoweringResult &result, const ABIModule &module, std::string &error) {
    if (result.types.empty() && result.functions.empty() && result.ownership.empty() && result.exposed.empty())
        return true;

    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> type_ids;
    for (const auto &type : module.types)
        type_ids[type.id.lo].insert(type.id.hi);
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> function_ids;
    for (const auto &function : module.functions)
        function_ids[function.signature.lo].insert(function.signature.hi);

    const auto has_type = [&](Hash128 id) {
        const auto lo = type_ids.find(id.lo);
        return lo != type_ids.end() && lo->second.count(id.hi) != 0;
    };
    const auto has_function = [&](Hash128 id) {
        const auto lo = function_ids.find(id.lo);
        return lo != function_ids.end() && lo->second.count(id.hi) != 0;
    };
    const auto missing_type = [&](const std::string &what, Hash128 id) {
        error = "lowering sidecar references an ABI type missing from the artifact: " + what + " (" + id_hex(id) + ")";
        return false;
    };
    const auto missing_function = [&](const std::string &what, Hash128 id) {
        error =
            "lowering sidecar references an ABI function missing from the artifact: " + what + " (" + id_hex(id) + ")";
        return false;
    };
    for (const auto &type : result.types) {
        if (type.status != LoweringStatus::LOWERED) continue;
        if (!valid(type.abi) || !has_type(type.abi)) return missing_type(type.name, type.abi);
        if (type.abi_index < module.types.size() && module.types[type.abi_index].id == type.abi) continue;
        return missing_type(type.name, type.abi);
    }
    for (const auto &function : result.functions) {
        if (function.status != LoweringStatus::LOWERED) continue;
        if (!valid(function.abi) || !has_function(function.abi)) return missing_function(function.name, function.abi);
        if (function.abi_index < module.functions.size()
            && module.functions[function.abi_index].signature == function.abi)
            continue;
        return missing_function(function.name, function.abi);
    }
    for (const auto &contract : result.ownership)
        if (!valid(contract.abi) || !has_function(contract.abi)) return missing_function("ownership", contract.abi);
    for (const auto &record : result.exposed) {
        if (record.status != LoweringStatus::LOWERED) continue;
        if (!valid(record.abi) || !has_type(record.abi)) return missing_type(record.name, record.abi);
        if (valid(record.create) && !has_function(record.create))
            return missing_function(record.name + "::create", record.create);
        if (valid(record.destroy) && !has_function(record.destroy))
            return missing_function(record.name + "::destroy", record.destroy);
        for (const auto &method : record.methods)
            if (!has_function(method.abi)) return missing_function(record.name + "::" + method.name, method.abi);
    }
    return true;
}

bool apply_lowering_contracts(const LoweringResult &result, ABIModule &module, std::string &error) {
    if (result.types.empty() && result.functions.empty() && result.ownership.empty() && result.exposed.empty())
        return true;

    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> type_ids;
    for (const auto &type : module.types)
        type_ids[type.id.lo].insert(type.id.hi);

    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> function_ids;
    std::vector<uint32_t> parameter_base(module.functions.size(), 0);
    uint32_t running = 0;
    for (size_t i = 0; i < module.functions.size(); ++i) {
        const auto &function = module.functions[i];
        function_ids[function.signature.lo].insert(function.signature.hi);
        parameter_base[i] = running;
        running += static_cast<uint32_t>(function.params.size());
    }

    const auto has_type = [&](Hash128 id) {
        const auto lo = type_ids.find(id.lo);
        return lo != type_ids.end() && lo->second.count(id.hi) != 0;
    };
    const auto has_function = [&](Hash128 id) {
        const auto lo = function_ids.find(id.lo);
        return lo != function_ids.end() && lo->second.count(id.hi) != 0;
    };
    if (!validate_lowering_references(result, module, error)) return false;

    const auto function_at = [&](Hash128 signature) -> const Function * {
        for (const auto &function : module.functions)
            if (function.signature == signature) return &function;
        return nullptr;
    };
    const auto function_slot = [&](Hash128 signature) -> int {
        for (size_t i = 0; i < module.functions.size(); ++i)
            if (module.functions[i].signature == signature) return static_cast<int>(i);
        return -1;
    };

    std::set<std::pair<uint64_t, uint64_t>> owned_types;
    // An exposed handle is a create/destroy pair even when no explicit
    // `[[owned]]` record was written by the frontend.
    for (const auto &record : result.exposed) {
        if (record.status != LoweringStatus::LOWERED
            || !has_type(record.abi)
            || !has_function(record.create)
            || !has_function(record.destroy))
            continue;
        if (!owned_types.emplace(record.abi.lo, record.abi.hi).second) continue;
        bool existing = false;
        for (const auto &owned : module.owned)
            if (owned.type_id == record.abi) {
                existing = true;
                break;
            }
        if (!existing) module.owned.push_back({record.abi, record.create, record.destroy, 0});
    }
    for (const auto &contract : result.ownership) {
        if (!has_function(contract.abi)) continue;
        const int slot = function_slot(contract.abi);
        if (slot < 0) continue;

        OwnershipContract applied;
        applied.target = contract.interaction == semantic::InteractionKind::RETURN ? OwnershipTarget::RETURN
                                                                                   : OwnershipTarget::PARAM;
        if (applied.target == OwnershipTarget::PARAM) {
            applied.target_index = parameter_base[static_cast<size_t>(slot)] + contract.index;
            if (contract.index >= module.functions[static_cast<size_t>(slot)].params.size()) continue;
        } else {
            applied.target_index = static_cast<uint32_t>(slot);
        }
        switch (contract.ownership) {
            case semantic::OwnershipKind::BORROWED:    applied.mode = OwnershipMode::BORROWED; break;
            case semantic::OwnershipKind::OWNED:       applied.mode = OwnershipMode::OWNED; break;
            case semantic::OwnershipKind::SHARED:      applied.mode = OwnershipMode::OWNED; break;
            case semantic::OwnershipKind::TRANSFERRED: applied.mode = OwnershipMode::OWNED; break;
            default:                                   applied.mode = OwnershipMode::UNSPECIFIED; break;
        }
        applied.release = contract.release;

        bool duplicate = false;
        for (const auto &existing : module.contracts)
            if (existing.target == applied.target
                && existing.target_index == applied.target_index
                && existing.mode == applied.mode
                && existing.release == applied.release) {
                duplicate = true;
                break;
            }
        if (!duplicate) module.contracts.push_back(applied);

        // An owned return value with a release entry point is exactly the
        // `[[owned]]` create/destroy pair a backend projects as a handle. The
        // lowering result names both ABI functions, so the backend does not
        // resolve either by spelling.
        if (applied.target == OwnershipTarget::RETURN
            && applied.mode == OwnershipMode::OWNED
            && valid(contract.release)
            && has_function(contract.release)) {
            const Function *create = function_at(contract.abi);
            if (create == nullptr || !has_type(create->return_type)) continue;
            if (!owned_types.emplace(create->return_type.lo, create->return_type.hi).second) continue;
            bool existing = false;
            for (const auto &record : module.owned)
                if (record.type_id == create->return_type) {
                    existing = true;
                    break;
                }
            if (!existing) module.owned.push_back({create->return_type, contract.abi, contract.release, 0});
        }
    }
    return true;
}

}   // namespace skl::amc::ir
