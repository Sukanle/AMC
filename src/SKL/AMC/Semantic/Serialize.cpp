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
#include "SKL/AMC/Semantic/Serialize.h"

#include "SKL/AMC/Support/Json.h"
#include "SKL/AMC/Semantic/Identity.h"

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <string_view>

#include <fmt/format.h>

#include "SKL/AMC/Semantic/Reflect.h"

namespace skl::amc::semantic {
namespace {

constexpr std::string_view semantic_schema = "amc.semantic/2";

using skl::amc::reflect::enum_name;
using skl::amc::reflect::enum_value;

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

Json source_json(const SourceLocation &source) {
    Json value = Json::object();
    value.set("file", source.file);
    value.set("line", static_cast<long long>(source.line));
    value.set("column", static_cast<long long>(source.column));
    return value;
}

Json parameter_json(const TemplateParam &parameter) {
    Json value = Json::object();
    value.set("kind", enum_name(parameter.kind, "type"));
    value.set("index", static_cast<long long>(parameter.index));
    value.set("name", parameter.name);
    if (parameter.default_argument.lo != 0 || parameter.default_argument.hi != 0)
        value.set("default_argument", id_hex(parameter.default_argument));
    if (parameter.constraint.lo != 0 || parameter.constraint.hi != 0)
        value.set("constraint", id_hex(parameter.constraint));
    if (!parameter.constraint_source.empty()) value.set("constraint_source", parameter.constraint_source);
    return value;
}

Json argument_json(const TemplateArg &argument) {
    Json value = Json::object();
    value.set("kind", enum_name(argument.kind, "other"));
    if (argument.type.valid()) value.set("type", id_hex(argument.type.value));
    if (argument.kind == decltype(argument.kind)::INTEGRAL) value.set("value", static_cast<long long>(argument.value));
    if (argument.template_decl.valid()) value.set("template_decl", id_hex(argument.template_decl.value));
    if (argument.canonical.lo != 0 || argument.canonical.hi != 0) value.set("canonical", id_hex(argument.canonical));
    return value;
}

SemanticTypeKind parse_type_kind(const std::string &name) {
    return enum_value<SemanticTypeKind>(name, SemanticTypeKind::UNKNOWN);
}

TemplateTypeKind parse_template_kind(const std::string &name) {
    return enum_value<TemplateTypeKind>(name, TemplateTypeKind::FUNC);
}

TemplateParamKind parse_parameter_kind(const std::string &name) {
    return enum_value<TemplateParamKind>(name, TemplateParamKind::TYPE);
}

TemplateArgKind parse_argument_kind(const std::string &name) {
    return enum_value<TemplateArgKind>(name, TemplateArgKind::TYPE);
}

InstOrigin parse_origin(const std::string &name) { return enum_value<InstOrigin>(name, InstOrigin::IMPLICIT_COMPILER); }

SemanticFuncKind parse_function_kind(const std::string &name) {
    return enum_value<SemanticFuncKind>(name, SemanticFuncKind::FREE);
}

OwnershipKind parse_ownership_kind(const std::string &name) {
    return enum_value<OwnershipKind>(name, OwnershipKind::UNSPECIFIED);
}

LifetimeKind parse_lifetime_kind(const std::string &name) {
    return enum_value<LifetimeKind>(name, LifetimeKind::UNKNOWN);
}

SourceLocation parse_source(const Json *node) {
    SourceLocation source;
    if (node == nullptr || !node->is_object()) return source;
    if (const auto *file = node->find("file"); file != nullptr) source.file = file->as_string();
    if (const auto *line = node->find("line"); line != nullptr) source.line = static_cast<uint32_t>(line->as_number());
    if (const auto *column = node->find("column"); column != nullptr)
        source.column = static_cast<uint32_t>(column->as_number());
    return source;
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

Json string_array_json(const std::vector<std::string> &values) {
    Json array = Json::array();
    for (const auto &value : values)
        array.push_back(value);
    return array;
}

Json rust_generic_parameters_json(const std::vector<RustGenericParam> &parameters) {
    Json array = Json::array();
    for (const auto &parameter : parameters) {
        Json value = Json::object();
        value.set("index", static_cast<long long>(parameter.index));
        value.set("name", parameter.name);
        value.set("kind", parameter.kind);
        if (!parameter.default_value.empty()) value.set("default", parameter.default_value);
        array.push_back(std::move(value));
    }
    return array;
}

Json rust_trait_method_json(const RustTraitMethod &method) {
    Json value = Json::object();
    value.set("name", method.name);
    value.set("path", method.path);
    value.set("abi", method.abi);
    value.set("return_type", method.return_type);
    value.set("has_receiver", method.has_receiver);
    value.set("required", method.required);
    Json parameters = Json::array();
    for (const auto &parameter : method.params) {
        Json item = Json::object();
        item.set("name", parameter.name);
        item.set("type", parameter.type);
        parameters.push_back(std::move(item));
    }
    value.set("parameters", std::move(parameters));
    return value;
}

void append_rust_json(Json &root, const RustSemantic &rust) {
    Json out = Json::object();

    Json generics = Json::array();
    for (const auto &generic : rust.generics) {
        Json value = Json::object();
        value.set("id", generic.id);
        value.set("name", generic.name);
        value.set("path", generic.path);
        value.set("kind", generic.kind);
        value.set("parameters", rust_generic_parameters_json(generic.params));
        value.set("where_clause", string_array_json(generic.where_clause));
        value.set("instances", string_array_json(generic.instances));
        generics.push_back(std::move(value));
    }
    out.set("generics", std::move(generics));

    Json traits = Json::array();
    for (const auto &trait : rust.traits) {
        Json value = Json::object();
        value.set("id", trait.id);
        value.set("name", trait.name);
        value.set("path", trait.path);
        value.set("kind", trait.kind);
        if (!trait.self_type.empty()) value.set("self_type", trait.self_type);
        if (!trait.trait_ref.empty()) value.set("trait", trait.trait_ref);
        value.set("negative", trait.negative);
        value.set("parameters", rust_generic_parameters_json(trait.params));
        value.set("where_clause", string_array_json(trait.where_clause));
        value.set("supertraits", string_array_json(trait.supertraits));
        Json associated = Json::array();
        for (const auto &item : trait.associated_types) {
            Json entry = Json::object();
            entry.set("name", item.name);
            entry.set("kind", item.kind);
            entry.set("bounds", string_array_json(item.bounds));
            associated.push_back(std::move(entry));
        }
        value.set("associated_types", std::move(associated));
        Json methods = Json::array();
        for (const auto &method : trait.methods)
            methods.push_back(rust_trait_method_json(method));
        value.set("methods", std::move(methods));
        traits.push_back(std::move(value));
    }
    out.set("traits", std::move(traits));

    Json errors = Json::array();
    for (const auto &error : rust.errors) {
        Json value = Json::object();
        value.set("id", error.id);
        value.set("name", error.name);
        value.set("path", error.path);
        value.set("kind", error.kind);
        Json variants = Json::array();
        for (const auto &variant : error.variants) {
            Json item = Json::object();
            item.set("name", variant.name);
            item.set("discriminant", variant.discriminant);
            if (!variant.payload.empty()) item.set("payload", variant.payload);
            variants.push_back(std::move(item));
        }
        value.set("variants", std::move(variants));
        value.set("functions", string_array_json(error.functions));
        errors.push_back(std::move(value));
    }
    out.set("errors", std::move(errors));

    Json lifetimes = Json::array();
    for (const auto &lifetime : rust.lifetimes) {
        Json value = Json::object();
        value.set("owner", lifetime.owner);
        value.set("name", lifetime.name);
        value.set("kind", lifetime.kind);
        value.set("bounds", string_array_json(lifetime.bounds));
        lifetimes.push_back(std::move(value));
    }
    out.set("lifetimes", std::move(lifetimes));

    Json trait_objects = Json::array();
    for (const auto &object : rust.trait_objects) {
        Json value = Json::object();
        value.set("owner", object.owner);
        value.set("context", object.context);
        value.set("type", object.type);
        value.set("traits", string_array_json(object.traits));
        trait_objects.push_back(std::move(value));
    }
    out.set("trait_objects", std::move(trait_objects));

    Json async_functions = Json::array();
    for (const auto &function : rust.async_functions) {
        Json value = Json::object();
        value.set("id", function.id);
        value.set("path", function.path);
        value.set("name", function.name);
        value.set("return_type", function.return_type);
        async_functions.push_back(std::move(value));
    }
    out.set("async_functions", std::move(async_functions));

    root.set("rust", std::move(out));
}

std::vector<std::string> read_string_array(const Json &value, const char *key) {
    std::vector<std::string> out;
    if (const Json *array = field(value, key); array != nullptr && array->is_array())
        for (const auto &item : array->items())
            out.push_back(item.as_string());
    return out;
}

std::vector<RustGenericParam> read_rust_generic_parameters(const Json &value) {
    std::vector<RustGenericParam> out;
    if (const Json *array = field(value, "parameters"); array != nullptr && array->is_array()) {
        for (const auto &item : array->items()) {
            RustGenericParam parameter;
            parameter.index = static_cast<uint32_t>(number_field(item, "index"));
            parameter.name = string_field(item, "name");
            parameter.kind = string_field(item, "kind");
            parameter.default_value = string_field(item, "default");
            out.push_back(std::move(parameter));
        }
    }
    return out;
}

void rust_generic(const Json &section, RustSemantic &rust) {
    if (const Json *generics = field(section, "generics"); generics != nullptr && generics->is_array()) {
        for (const auto &value : generics->items()) {
            RustGeneric generic;
            generic.id = string_field(value, "id");
            generic.name = string_field(value, "name");
            generic.path = string_field(value, "path");
            generic.kind = string_field(value, "kind");
            generic.params = read_rust_generic_parameters(value);
            generic.where_clause = read_string_array(value, "where_clause");
            generic.instances = read_string_array(value, "instances");
            rust.generics.push_back(std::move(generic));
        }
    }
}

void rust_traits(const Json &section, RustSemantic &rust) {
    if (const Json *traits = field(section, "traits"); traits != nullptr && traits->is_array()) {
        for (const auto &value : traits->items()) {
            RustTrait trait;
            trait.id = string_field(value, "id");
            trait.name = string_field(value, "name");
            trait.path = string_field(value, "path");
            trait.kind = string_field(value, "kind");
            trait.self_type = string_field(value, "self_type");
            trait.trait_ref = string_field(value, "trait");
            trait.negative = bool_field(value, "negative");
            trait.params = read_rust_generic_parameters(value);
            trait.where_clause = read_string_array(value, "where_clause");
            trait.supertraits = read_string_array(value, "supertraits");
            if (const Json *associated = field(value, "associated_types");
                associated != nullptr && associated->is_array()) {
                for (const auto &item : associated->items()) {
                    RustAssociatedType entry;
                    entry.name = string_field(item, "name");
                    entry.kind = string_field(item, "kind");
                    entry.bounds = read_string_array(item, "bounds");
                    trait.associated_types.push_back(std::move(entry));
                }
            }
            if (const Json *methods = field(value, "methods"); methods != nullptr && methods->is_array()) {
                for (const auto &item : methods->items()) {
                    RustTraitMethod method;
                    method.name = string_field(item, "name");
                    method.path = string_field(item, "path");
                    method.abi = string_field(item, "abi");
                    method.return_type = string_field(item, "return_type");
                    method.has_receiver = bool_field(item, "has_receiver");
                    method.required = bool_field(item, "required");
                    if (const Json *parameters = field(item, "parameters");
                        parameters != nullptr && parameters->is_array()) {
                        for (const auto &parameter : parameters->items()) {
                            RustTraitParameter entry;
                            entry.name = string_field(parameter, "name");
                            entry.type = string_field(parameter, "type");
                            method.params.push_back(std::move(entry));
                        }
                    }
                    trait.methods.push_back(std::move(method));
                }
            }
            rust.traits.push_back(std::move(trait));
        }
    }
}

void rust_errors(const Json &section, RustSemantic &rust) {

    if (const Json *errors = field(section, "errors"); errors != nullptr && errors->is_array()) {
        for (const auto &value : errors->items()) {
            RustError error;
            error.id = string_field(value, "id");
            error.name = string_field(value, "name");
            error.path = string_field(value, "path");
            error.kind = string_field(value, "kind");
            if (const Json *variants = field(value, "variants"); variants != nullptr && variants->is_array()) {
                for (const auto &item : variants->items()) {
                    RustErrorVar variant;
                    variant.name = string_field(item, "name");
                    variant.discriminant = string_field(item, "discriminant");
                    variant.payload = string_field(item, "payload");
                    error.variants.push_back(std::move(variant));
                }
            }
            error.functions = read_string_array(value, "functions");
            rust.errors.push_back(std::move(error));
        }
    }
}

void rust_lifetime(const Json &section, RustSemantic &rust) {

    if (const Json *lifetimes = field(section, "lifetimes"); lifetimes != nullptr && lifetimes->is_array()) {
        for (const auto &value : lifetimes->items()) {
            RustLifetime lifetime;
            lifetime.owner = string_field(value, "owner");
            lifetime.name = string_field(value, "name");
            lifetime.kind = string_field(value, "kind");
            lifetime.bounds = read_string_array(value, "bounds");
            rust.lifetimes.push_back(std::move(lifetime));
        }
    }
}

void rust_traits_obj(const Json &section, RustSemantic &rust) {

    if (const Json *objects = field(section, "trait_objects"); objects != nullptr && objects->is_array()) {
        for (const auto &value : objects->items()) {
            RustTraitObj object;
            object.owner = string_field(value, "owner");
            object.context = string_field(value, "context");
            object.type = string_field(value, "type");
            object.traits = read_string_array(value, "traits");
            rust.trait_objects.push_back(std::move(object));
        }
    }
}

void rust_async(const Json &section, RustSemantic &rust) {

    if (const Json *functions = field(section, "async_functions"); functions != nullptr && functions->is_array()) {
        for (const auto &value : functions->items()) {
            RustAsyncFunc function;
            function.id = string_field(value, "id");
            function.path = string_field(value, "path");
            function.name = string_field(value, "name");
            function.return_type = string_field(value, "return_type");
            rust.async_functions.push_back(std::move(function));
        }
    }
}

void read_rust_json(const Json &root, RustSemantic &rust) {
    const Json *section = field(root, "rust");
    if (section == nullptr || !section->is_object()) return;

    rust_generic(*section, rust);
    rust_traits(*section, rust);
    rust_errors(*section, rust);
    rust_lifetime(*section, rust);
    rust_traits_obj(*section, rust);
}

}   // namespace

bool write_semantic_json(const SemanticModule &module, const std::string &path, std::string &error) {
    Json root = Json::object();
    root.set("schema", std::string(semantic_schema));
    root.set("rules_version", static_cast<long long>(module.rules_version));
    root.set("package", module.package_name);
    root.set("version", module.package_version);

    Json types = Json::array();
    for (const auto &type : module.types) {
        Json value = Json::object();
        value.set("id", id_hex(type.id.value));
        value.set("kind", enum_name(type.kind, "unknown"));
        value.set("name", type.qualified);
        value.set("const", type.qualifiers.is_const);
        value.set("volatile", type.qualifiers.is_volatile);
        if (type.canonical.valid()) value.set("canonical", id_hex(type.canonical.value));
        if (type.element.valid()) value.set("element", id_hex(type.element.value));
        if (type.template_inst.valid()) value.set("template_instance", id_hex(type.template_inst.value));
        if (type.abi_type.lo != 0 || type.abi_type.hi != 0) value.set("abi_type", id_hex(type.abi_type));
        types.push_back(std::move(value));
    }
    root.set("types", std::move(types));

    Json records = Json::array();
    for (const auto &record : module.records) {
        Json value = Json::object();
        value.set("type", id_hex(record.type.value));
        value.set("polymorphic", record.is_polymorphic);
        value.set("abstract", record.is_abstract);
        value.set("template_primary", record.is_template_primary);
        Json bases = Json::array();
        for (const auto &base : record.bases) {
            Json item = Json::object();
            item.set("type", id_hex(base.type.value));
            item.set("bit_offset", static_cast<long long>(base.bit_offset));
            item.set("visibility", static_cast<long long>(base.visibility));
            item.set("virtual", base.is_virtual);
            bases.push_back(std::move(item));
        }
        value.set("bases", std::move(bases));
        Json fields = Json::array();
        for (const auto &field : record.fields) {
            Json item = Json::object();
            item.set("name", field.name);
            item.set("type", id_hex(field.type.value));
            item.set("bit_offset", static_cast<long long>(field.bit_offset));
            item.set("bit_width", static_cast<long long>(field.bit_width));
            item.set("visibility", static_cast<long long>(field.visibility));
            item.set("static", field.is_static);
            item.set("mutable", field.is_mutable);
            fields.push_back(std::move(item));
        }
        value.set("fields", std::move(fields));
        value.set("source", source_json(record.source));
        records.push_back(std::move(value));
    }
    root.set("records", std::move(records));

    Json functions = Json::array();
    for (const auto &function : module.functions) {
        Json value = Json::object();
        value.set("id", id_hex(function.id.value));
        value.set("kind", enum_name(function.kind, "free_function"));
        value.set("name", function.qualified);
        value.set("unqualified_name", function.unqualified);
        if (function.owner.valid()) value.set("owner", id_hex(function.owner.value));
        if (function.return_type.valid()) value.set("return_type", id_hex(function.return_type.value));
        value.set("variadic", function.is_variadic);
        value.set("noexcept", function.is_noexcept);
        value.set("static", function.is_static);
        value.set("virtual", function.is_virtual);
        value.set("constexpr", function.is_constexpr);
        value.set("template_primary", function.is_template_primary);
        if (function.template_instance.valid())
            value.set("template_instance", id_hex(function.template_instance.value));
        if (function.abi_signature.lo != 0 || function.abi_signature.hi != 0)
            value.set("abi_signature", id_hex(function.abi_signature));
        Json parameters = Json::array();
        for (const auto &parameter : function.params) {
            Json item = Json::object();
            item.set("name", parameter.name);
            item.set("type", id_hex(parameter.type.value));
            item.set("category", static_cast<long long>(parameter.category));
            item.set("variadic", parameter.is_variadic);
            if (!parameter.default_value.empty()) item.set("default", parameter.default_value);
            parameters.push_back(std::move(item));
        }
        value.set("parameters", std::move(parameters));
        value.set("source", source_json(function.source));
        functions.push_back(std::move(value));
    }
    root.set("functions", std::move(functions));

    Json ownership = Json::array();
    for (const auto &contract : module.ownership) {
        Json value = Json::object();
        value.set("function", id_hex(contract.function.value));
        value.set("interaction", enum_name(contract.interaction, "parameter"));
        value.set("index", static_cast<long long>(contract.index));
        value.set("ownership", enum_name(contract.ownership, "unspecified"));
        value.set("lifetime", enum_name(contract.lifetime, "unknown"));
        value.set("mutability", enum_name(contract.mutability, "immutable"));
        if (contract.release.valid()) value.set("release", id_hex(contract.release.value));
        if (!contract.invalidation.empty()) value.set("invalidation", contract.invalidation);
        ownership.push_back(std::move(value));
    }
    root.set("ownership", std::move(ownership));

    Json templates = Json::array();
    for (const auto &declaration : module.templates) {
        Json value = Json::object();
        value.set("id", id_hex(declaration.id.value));
        value.set("kind", enum_name(declaration.kind, "function"));
        value.set("name", declaration.qualified_name);
        if (!declaration.requires_source.empty()) value.set("requires", declaration.requires_source);
        if (declaration.requires_hash.lo != 0 || declaration.requires_hash.hi != 0)
            value.set("requires_hash", id_hex(declaration.requires_hash));
        Json parameters = Json::array();
        for (const auto &parameter : declaration.parameters)
            parameters.push_back(parameter_json(parameter));
        value.set("parameters", std::move(parameters));
        value.set("source", source_json(declaration.source));
        templates.push_back(std::move(value));
    }
    Json exposed = Json::array();
    for (const auto &record : module.exposed) {
        Json value = Json::object();
        if (record.type.valid()) value.set("type", id_hex(record.type.value));
        if (record.abi_type.lo != 0 || record.abi_type.hi != 0) value.set("abi_type", id_hex(record.abi_type));
        value.set("name", record.qualified);
        value.set("symbol", record.symbol);
        value.set("constructor", record.has_constructor);
        value.set("opaque", record.opaque);
        if (record.create.lo != 0 || record.create.hi != 0) value.set("create", id_hex(record.create));
        if (record.destroy.lo != 0 || record.destroy.hi != 0) value.set("destroy", id_hex(record.destroy));
        Json methods = Json::array();
        for (const auto &method : record.methods) {
            Json item = Json::object();
            item.set("name", method.name);
            item.set("symbol", method.symbol);
            item.set("signature", id_hex(method.abi_signature));
            methods.push_back(std::move(item));
        }
        value.set("methods", std::move(methods));
        exposed.push_back(std::move(value));
    }
    root.set("exposed", std::move(exposed));

    root.set("templates", std::move(templates));

    Json instances = Json::array();
    for (const auto &instance : module.instances) {
        Json value = Json::object();
        value.set("id", id_hex(instance.id.value));
        value.set("declaration", id_hex(instance.declaration.value));
        value.set("origin", enum_name(instance.origin, "implicit_compiler"));
        value.set("explicit_specialization", instance.is_explicit_specialization);
        if (instance.result.lo != 0 || instance.result.hi != 0) value.set("result", id_hex(instance.result));
        Json arguments = Json::array();
        for (const auto &argument : instance.args)
            arguments.push_back(argument_json(argument));
        value.set("arguments", std::move(arguments));
        Json constraint = Json::object();
        constraint.set("evaluated", instance.constraint.evaluated);
        constraint.set("satisfied", instance.constraint.satisfied);
        if (!instance.constraint.reason.empty()) constraint.set("reason", instance.constraint.reason);
        value.set("constraint", std::move(constraint));
        value.set("source", source_json(instance.source));
        instances.push_back(std::move(value));
    }
    root.set("instances", std::move(instances));
    append_rust_json(root, module.rust);

    try {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot write semantic sidecar: " + path;
            return false;
        }
        output << root.dump_pretty(2) << '\n';
        if (!output.good()) {
            error = "cannot write semantic sidecar: " + path;
            return false;
        }
    } catch (const std::exception &e) {
        error = "cannot write semantic sidecar: " + path + ": " + e.what();
        return false;
    }
    return true;
}

bool read_semantic_json(const std::string &path, SemanticModule &module, std::string &error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot read semantic sidecar: " + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    Json root;
    if (!parse_json(text, root, error)) {
        error = "invalid semantic sidecar " + path + ": " + error;
        return false;
    }
    if (!root.is_object()) {
        error = "invalid semantic sidecar " + path + ": root must be an object";
        return false;
    }
    const std::string schema = string_field(root, "schema");
    if (schema != "amc.semantic/1" && schema != "amc.semantic/2") {
        error = "unsupported semantic sidecar schema: " + schema;
        return false;
    }

    module = {};
    module.rules_version = static_cast<uint32_t>(number_field(root, "rules_version"));
    if (module.rules_version == 0) module.rules_version = semantic_rules_version;
    module.package_name = string_field(root, "package");
    module.package_version = string_field(root, "version");

    if (const Json *types = field(root, "types"); types != nullptr && types->is_array()) {
        for (const auto &value : types->items()) {
            SemanticType type;
            type.id = SemanticTypeId{hash_field(value, "id")};
            type.kind = parse_type_kind(string_field(value, "kind"));
            type.qualified = string_field(value, "name");
            type.qualifiers.is_const = bool_field(value, "const");
            type.qualifiers.is_volatile = bool_field(value, "volatile");
            type.canonical = SemanticTypeId{hash_field(value, "canonical")};
            type.element = SemanticTypeId{hash_field(value, "element")};
            type.template_inst = TemplateInstId{hash_field(value, "template_instance")};
            type.abi_type = hash_field(value, "abi_type");
            module.types.push_back(std::move(type));
        }
    }

    if (const Json *records = field(root, "records"); records != nullptr && records->is_array()) {
        for (const auto &value : records->items()) {
            SemanticRecord record;
            record.type = SemanticTypeId{hash_field(value, "type")};
            record.is_polymorphic = bool_field(value, "polymorphic");
            record.is_abstract = bool_field(value, "abstract");
            record.is_template_primary = bool_field(value, "template_primary");
            if (const Json *bases = field(value, "bases"); bases != nullptr && bases->is_array()) {
                for (const auto &item : bases->items()) {
                    SemanticBase base;
                    base.type = SemanticTypeId{hash_field(item, "type")};
                    base.bit_offset = static_cast<uint32_t>(number_field(item, "bit_offset"));
                    base.visibility = static_cast<Visibility>(number_field(item, "visibility"));
                    base.is_virtual = bool_field(item, "virtual");
                    record.bases.push_back(std::move(base));
                }
            }
            if (const Json *fields = field(value, "fields"); fields != nullptr && fields->is_array()) {
                for (const auto &item : fields->items()) {
                    SemanticField field;
                    field.name = string_field(item, "name");
                    field.type = SemanticTypeId{hash_field(item, "type")};
                    field.bit_offset = static_cast<uint32_t>(number_field(item, "bit_offset"));
                    field.bit_width = static_cast<uint32_t>(number_field(item, "bit_width"));
                    field.visibility = static_cast<Visibility>(number_field(item, "visibility"));
                    field.is_static = bool_field(item, "static");
                    field.is_mutable = bool_field(item, "mutable");
                    record.fields.push_back(std::move(field));
                }
            }
            record.source = parse_source(field(value, "source"));
            module.records.push_back(std::move(record));
        }
    }

    if (const Json *functions = field(root, "functions"); functions != nullptr && functions->is_array()) {
        for (const auto &value : functions->items()) {
            SemanticFunc function;
            function.id = SemanticFuncId{hash_field(value, "id")};
            function.kind = parse_function_kind(string_field(value, "kind"));
            function.qualified = string_field(value, "name");
            function.unqualified = string_field(value, "unqualified_name");
            function.owner = SemanticTypeId{hash_field(value, "owner")};
            function.return_type = SemanticTypeId{hash_field(value, "return_type")};
            function.is_variadic = bool_field(value, "variadic");
            function.is_noexcept = bool_field(value, "noexcept");
            function.is_static = bool_field(value, "static");
            function.is_virtual = bool_field(value, "virtual");
            function.is_constexpr = bool_field(value, "constexpr");
            function.is_template_primary = bool_field(value, "template_primary");
            function.template_instance = TemplateInstId{hash_field(value, "template_instance")};
            function.abi_signature = hash_field(value, "abi_signature");
            if (const Json *parameters = field(value, "parameters"); parameters != nullptr && parameters->is_array()) {
                for (const auto &item : parameters->items()) {
                    SemanticParam parameter;
                    parameter.name = string_field(item, "name");
                    parameter.type = SemanticTypeId{hash_field(item, "type")};
                    parameter.category = static_cast<ValueCategory>(number_field(item, "category"));
                    parameter.is_variadic = bool_field(item, "variadic");
                    parameter.default_value = string_field(item, "default");
                    function.params.push_back(std::move(parameter));
                }
            }
            function.source = parse_source(field(value, "source"));
            module.functions.push_back(std::move(function));
        }
    }

    if (const Json *ownership = field(root, "ownership"); ownership != nullptr && ownership->is_array()) {
        for (const auto &value : ownership->items()) {
            SemanticOwner contract;
            contract.function = SemanticFuncId{hash_field(value, "function")};
            contract.interaction =
                string_field(value, "interaction") == "return_value" ? InteractionKind::RETURN : InteractionKind::PARAM;
            contract.index = static_cast<uint32_t>(number_field(value, "index"));
            contract.ownership = parse_ownership_kind(string_field(value, "ownership"));
            contract.lifetime = parse_lifetime_kind(string_field(value, "lifetime"));
            contract.mutability =
                string_field(value, "mutability") == "mutable" ? Mutability::MUTABLE : Mutability::IMMUTABLE;
            contract.release = SemanticFuncId{hash_field(value, "release")};
            contract.invalidation = string_field(value, "invalidation");
            module.ownership.push_back(std::move(contract));
        }
    }

    if (const Json *exposed = field(root, "exposed"); exposed != nullptr && exposed->is_array()) {
        for (const auto &value : exposed->items()) {
            ExposedType record;
            record.type = SemanticTypeId{hash_field(value, "type")};
            record.abi_type = hash_field(value, "abi_type");
            record.qualified = string_field(value, "name");
            record.symbol = string_field(value, "symbol");
            record.has_constructor = bool_field(value, "constructor");
            record.opaque = bool_field(value, "opaque");
            record.create = hash_field(value, "create");
            record.destroy = hash_field(value, "destroy");
            if (const Json *methods = field(value, "methods"); methods != nullptr && methods->is_array()) {
                for (const auto &item : methods->items()) {
                    ExposedMethod method;
                    method.name = string_field(item, "name");
                    method.symbol = string_field(item, "symbol");
                    method.abi_signature = hash_field(item, "signature");
                    record.methods.push_back(std::move(method));
                }
            }
            module.exposed.push_back(std::move(record));
        }
    }

    if (const Json *templates = field(root, "templates"); templates != nullptr && templates->is_array()) {
        for (const auto &value : templates->items()) {
            TemplateDecl declaration;
            declaration.id = TemplateDeclId{hash_field(value, "id")};
            declaration.kind = parse_template_kind(string_field(value, "kind"));
            declaration.qualified_name = string_field(value, "name");
            declaration.requires_source = string_field(value, "requires");
            declaration.requires_hash = hash_field(value, "requires_hash");
            if (const Json *parameters = field(value, "parameters"); parameters != nullptr && parameters->is_array()) {
                for (const auto &item : parameters->items()) {
                    TemplateParam parameter;
                    parameter.kind = parse_parameter_kind(string_field(item, "kind"));
                    parameter.index = static_cast<uint32_t>(number_field(item, "index"));
                    parameter.name = string_field(item, "name");
                    parameter.default_argument = hash_field(item, "default_argument");
                    parameter.constraint = hash_field(item, "constraint");
                    parameter.constraint_source = string_field(item, "constraint_source");
                    declaration.parameters.push_back(std::move(parameter));
                }
            }
            declaration.source = parse_source(field(value, "source"));
            module.templates.push_back(std::move(declaration));
        }
    }

    if (const Json *instances = field(root, "instances"); instances != nullptr && instances->is_array()) {
        for (const auto &value : instances->items()) {
            TemplateInst instance;
            instance.id = TemplateInstId{hash_field(value, "id")};
            instance.declaration = TemplateDeclId{hash_field(value, "declaration")};
            instance.origin = parse_origin(string_field(value, "origin"));
            instance.is_explicit_specialization = bool_field(value, "explicit_specialization");
            instance.result = hash_field(value, "result");
            if (const Json *arguments = field(value, "arguments"); arguments != nullptr && arguments->is_array()) {
                for (const auto &item : arguments->items()) {
                    TemplateArg argument;
                    argument.kind = parse_argument_kind(string_field(item, "kind"));
                    argument.type = SemanticTypeId{hash_field(item, "type")};
                    argument.value = number_field(item, "value");
                    argument.template_decl = TemplateDeclId{hash_field(item, "template_decl")};
                    argument.canonical = hash_field(item, "canonical");
                    instance.args.push_back(std::move(argument));
                }
            }
            if (const Json *constraint = field(value, "constraint"); constraint != nullptr) {
                instance.constraint.evaluated = bool_field(*constraint, "evaluated");
                instance.constraint.satisfied = bool_field(*constraint, "satisfied");
                instance.constraint.reason = string_field(*constraint, "reason");
            }
            instance.source = parse_source(field(value, "source"));
            module.instances.push_back(std::move(instance));
        }
    }

    read_rust_json(root, module.rust);
    return true;
}

}   // namespace skl::amc::semantic
