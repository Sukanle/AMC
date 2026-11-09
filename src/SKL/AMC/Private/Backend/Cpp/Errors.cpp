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
#include "SKL/AMC/Private/Backend/Cpp/Emitters.h"

#include "SKL/AMC/Private/Backend/Cpp/CppNames.h"
#include "SKL/AMC/Core/Core.h"

#include <stdint.h>
#include <set>
#include <string>
namespace skl::amc::lang::backend::cpp {
namespace {
std::string parameter_name(const amc::Parameter &parameter, size_t index) {
    return parameter.name.empty() ? fmt::format("arg{}", index) : cpp_name(parameter.name);
}
}   // namespace
void Emit::error_set(const amc::ErrorSet &set) {
    const std::string name = cpp_name(set.name);
    _o.print(
        "\n// Named error set `{0}` (domain 0x{1:016x}).\n"
        "struct {2} {{\n"
        "    int value = 0;\n"
        "    constexpr {2}() = default;\n"
        "    constexpr explicit {2}(int raw) : value(raw) {{}}\n"
        "    static constexpr unsigned long long domain = 0x{3:x};\n",
        set.name, set.domain, name, set.domain);

    for (uint32_t i = 0; i < set.code_count; ++i) {
        const auto &code = _m.error_codes[set.code_begin + i];
        _o.print("    static constexpr int {} = {};\n", cpp_name(code.name), code.value);
    }
    _o.print("    static constexpr const char *name(int raw) noexcept {{\n        switch (raw) {{\n");
    for (uint32_t i = 0; i < set.code_count; ++i) {
        const auto &code = _m.error_codes[set.code_begin + i];
        _o.print("            case {}: return \"{}\";\n", code.value, code.name);
    }
    _o.print(
        "            default: return \"unknown\";\n"
        "        }}\n"
        "    }}\n"
        "    static constexpr int condition(int raw) noexcept {{\n"
        "    switch (raw) {{\n");
    for (uint32_t i = 0; i < set.code_count; ++i) {
        const auto &code = _m.error_codes[set.code_begin + i];
        _o.print("            case {}: return {};\n", code.value, code.condition);
    }
    _o.print(
        "            default: return -1;\n"
        "        }}\n"
        "    }}\n"
        "    static constexpr const char *condition_name(int raw) noexcept {{\n"
        "        switch (condition(raw)) {{\n");
    std::set<int32_t> conditions;
    for (uint32_t i = 0; i < set.code_count; ++i)
        conditions.insert(_m.error_codes[set.code_begin + i].condition);
    for (const int32_t condition : conditions)
        _o.print("            case {}: return \"{}\";\n", condition,
            amc::generic_condition_name(static_cast<amc::GenericCondition>(condition)));
    _o.print(
        "            default: return \"unknown\";\n"
        "        }}\n"
        "    }}\n"
        "}};\n");
}

void Emit::named_error_sets() {
    // Named error sets: stable numeric codes, names and generic conditions.
    // Unknown values are preserved by carrying the raw status in `value`.
    for (const auto &set : _m.error_sets)
        error_set(set);
}

void Emit::error_boundaries() {
    // Error-boundary wrappers: turn the declared C status into a typed
    // `Result<T, E>` instead of exposing a bare status code.
    for (const auto &record : _m.errors) {
        if (record.function_index >= _m.functions.size()) continue;
        const amc::Function &function = _m.functions[record.function_index];
        const auto find_type = [&](amc::Hash128 id) -> const amc::Type * {
            for (const auto &type : _m.types)
                if (type.id == id) return &type;
            return nullptr;
        };
        const auto pointee_of = [&](amc::Hash128 id) -> amc::Hash128 {
            for (const auto &ref : _m.pointees)
                if (ref.pointer == id) return ref.pointee;
            return {};
        };
        const auto parameter_name = [](const amc::Parameter &parameter, size_t index) {
            return parameter.name.empty() ? fmt::format("arg{}", index) : cpp_name(parameter.name);
        };

        const auto find_error_set = [&](amc::Hash128 id) -> const amc::ErrorSet * {
            if (id.lo == 0 && id.hi == 0) return nullptr;
            for (const auto &set : _m.error_sets)
                if (set.id == id) return &set;
            return nullptr;
        };
        std::string value_type, status_type;
        std::string status_access, value_access;
        std::string params, call_args;
        if (record.status_source == amc::StatusSource::RETURN_FIELD) {
            const amc::Type *result = find_type(function.return_type);
            if (result == nullptr
                || record.status_parameter >= result->field_count
                || record.value_parameter >= result->field_count)
                continue;
            const auto &status_field = _m.fields[result->field_begin + record.status_parameter];
            const auto &value_field = _m.fields[result->field_begin + record.value_parameter];
            value_type = cpp_type_of(_m, value_field.type_id);
            status_type = cpp_type_of(_m, status_field.type_id);
            status_access = cpp_name(status_field.name);
            value_access = cpp_name(value_field.name);
            for (size_t i = 0; i < function.params.size(); ++i) {
                if (i != 0) {
                    params += ", ";
                    call_args += ", ";
                }
                const std::string name = parameter_name(function.params[i], i);
                params += cpp_type_of(_m, function.params[i].type_id) + " " + name;
                call_args += name;
            }
        } else if (record.status_source == amc::StatusSource::RETURN_VALUE) {
            if (record.value_parameter == UINT32_MAX || record.value_parameter >= function.params.size()) continue;
            const amc::Hash128 value_id = pointee_of(function.params[record.value_parameter].type_id);
            if (value_id.lo == 0 && value_id.hi == 0) continue;
            value_type = cpp_type_of(_m, value_id);
            status_type = cpp_type_of(_m, function.return_type);
            for (size_t i = 0; i < function.params.size(); ++i) {
                if (i == record.value_parameter) {
                    call_args += (call_args.empty() ? "" : ", ") + std::string("&value");
                    continue;
                }
                if (!params.empty()) {
                    params += ", ";
                    call_args += ", ";
                }
                const std::string name = parameter_name(function.params[i], i);
                params += cpp_type_of(_m, function.params[i].type_id) + " " + name;
                call_args += name;
            }
        } else
            continue;


        const amc::ErrorSet *error_set = find_error_set(record.error_set);
        const std::string error_type = error_set != nullptr ? cpp_name(error_set->name) : status_type;
        _o.print("\n// Typed error boundary for `{}`.\n", function.name);
        _o.print("inline Result<{}, {}> {}_result({}) {{\n", value_type, error_type, function.name, params);
        if (record.status_source == amc::StatusSource::RETURN_VALUE) _o.print("    {} value{{}};\n", value_type);
        if (record.status_source == amc::StatusSource::RETURN_FIELD)
            _o.print("    auto raw = {}({});\n", function.name, call_args);
        else
            _o.print("    auto status = {}({});\n", function.name, call_args);
        if (record.status_source == amc::StatusSource::RETURN_FIELD)
            _o.print("    if (raw.{} == static_cast<{}>({})) return {{true, raw.{}, {{}}}};\n", status_access,
                status_type, record.success_value, value_access);
        else
            _o.print("    if (status == static_cast<{}>({})) return {{true, value, {{}}}};\n", status_type,
                record.success_value);
        if (record.status_source == amc::StatusSource::RETURN_FIELD)
            _o.print("    return {{false, {{}}, {}(raw.{})}};\n", error_type, status_access);
        else
            _o.print("    return {{false, {{}}, {}(status)}};\n", error_type);
        _o.print("}}\n");
    }
}

}   // namespace skl::amc::lang::backend::cpp
