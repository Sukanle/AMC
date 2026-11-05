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

#include "SKL/AMC/Core/Json.h"

#include <exception>
#include <fstream>

#include <fmt/format.h>

namespace amc::semantic {
namespace {

std::string id_hex(Hash128 value) { return fmt::format("{:016x}{:016x}", value.hi, value.lo); }

const char *type_kind_name(SemanticTypeKind kind) {
    switch (kind) {
        case SemanticTypeKind::builtin:            return "builtin";
        case SemanticTypeKind::pointer:            return "pointer";
        case SemanticTypeKind::lvalue_reference:   return "lvalue_reference";
        case SemanticTypeKind::rvalue_reference:   return "rvalue_reference";
        case SemanticTypeKind::array:              return "array";
        case SemanticTypeKind::record:             return "record";
        case SemanticTypeKind::enumeration:        return "enumeration";
        case SemanticTypeKind::function:           return "function";
        case SemanticTypeKind::member_pointer:     return "member_pointer";
        case SemanticTypeKind::template_parameter: return "template_parameter";
        case SemanticTypeKind::template_instance:  return "template_instance";
        case SemanticTypeKind::dependent:          return "dependent";
        case SemanticTypeKind::unknown:            return "unknown";
    }
    return "unknown";
}

const char *template_kind_name(TemplateKind kind) {
    switch (kind) {
        case TemplateKind::function: return "function";
        case TemplateKind::class_:   return "class";
        case TemplateKind::alias:    return "alias";
        case TemplateKind::variable: return "variable";
    }
    return "function";
}

const char *parameter_kind_name(TemplateParameterKind kind) {
    switch (kind) {
        case TemplateParameterKind::type:              return "type";
        case TemplateParameterKind::non_type:          return "non_type";
        case TemplateParameterKind::template_template: return "template_template";
    }
    return "type";
}

const char *argument_kind_name(TemplateArgumentKind kind) {
    switch (kind) {
        case TemplateArgumentKind::type:      return "type";
        case TemplateArgumentKind::integral:  return "integral";
        case TemplateArgumentKind::template_: return "template";
        case TemplateArgumentKind::pack:      return "pack";
        case TemplateArgumentKind::other:     return "other";
    }
    return "other";
}

const char *origin_name(InstantiationOrigin origin) {
    switch (origin) {
        case InstantiationOrigin::implicit_compiler: return "implicit_compiler";
        case InstantiationOrigin::explicit_source:   return "explicit_source";
        case InstantiationOrigin::amc_requested:     return "amc_requested";
        case InstantiationOrigin::config_requested:  return "config_requested";
    }
    return "implicit_compiler";
}

Json source_json(const SourceLocation &source) {
    Json value = Json::object();
    value.set("file", source.file);
    value.set("line", static_cast<long long>(source.line));
    value.set("column", static_cast<long long>(source.column));
    return value;
}

Json parameter_json(const TemplateParameter &parameter) {
    Json value = Json::object();
    value.set("kind", parameter_kind_name(parameter.kind));
    value.set("index", static_cast<long long>(parameter.index));
    value.set("name", parameter.name);
    if (parameter.default_argument.lo != 0 || parameter.default_argument.hi != 0)
        value.set("default_argument", id_hex(parameter.default_argument));
    if (parameter.constraint.lo != 0 || parameter.constraint.hi != 0)
        value.set("constraint", id_hex(parameter.constraint));
    if (!parameter.constraint_source.empty()) value.set("constraint_source", parameter.constraint_source);
    return value;
}

Json argument_json(const TemplateArgument &argument) {
    Json value = Json::object();
    value.set("kind", argument_kind_name(argument.kind));
    if (argument.type.valid()) value.set("type", id_hex(argument.type.value));
    if (argument.kind == TemplateArgumentKind::integral) value.set("value", static_cast<long long>(argument.value));
    if (argument.template_decl.valid()) value.set("template_decl", id_hex(argument.template_decl.value));
    if (argument.canonical.lo != 0 || argument.canonical.hi != 0) value.set("canonical", id_hex(argument.canonical));
    return value;
}

}   // namespace

bool write_semantic_json(const SemanticModule &module, const std::string &path, std::string &error) {
    Json root = Json::object();
    root.set("schema", "amc.semantic/1");
    root.set("rules_version", static_cast<long long>(module.rules_version));
    root.set("package", module.package_name);
    root.set("version", module.package_version);

    Json types = Json::array();
    for (const auto &type : module.types) {
        Json value = Json::object();
        value.set("id", id_hex(type.id.value));
        value.set("kind", type_kind_name(type.kind));
        value.set("name", type.qualified_name);
        value.set("const", type.qualifiers.is_const);
        value.set("volatile", type.qualifiers.is_volatile);
        if (type.canonical.valid()) value.set("canonical", id_hex(type.canonical.value));
        if (type.element.valid()) value.set("element", id_hex(type.element.value));
        if (type.template_instance.valid()) value.set("template_instance", id_hex(type.template_instance.value));
        types.push_back(std::move(value));
    }
    root.set("types", std::move(types));

    Json templates = Json::array();
    for (const auto &declaration : module.templates) {
        Json value = Json::object();
        value.set("id", id_hex(declaration.id.value));
        value.set("kind", template_kind_name(declaration.kind));
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
    root.set("templates", std::move(templates));

    Json instances = Json::array();
    for (const auto &instance : module.instances) {
        Json value = Json::object();
        value.set("id", id_hex(instance.id.value));
        value.set("declaration", id_hex(instance.declaration.value));
        value.set("origin", origin_name(instance.origin));
        value.set("explicit_specialization", instance.is_explicit_specialization);
        if (instance.result.lo != 0 || instance.result.hi != 0) value.set("result", id_hex(instance.result));
        Json arguments = Json::array();
        for (const auto &argument : instance.arguments)
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

}   // namespace amc::semantic
