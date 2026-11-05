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
#include "SKL/AMC/IR/Lowering.h"

#include <string>

namespace amc::lang::backend::cpp {

const amc::Function *Emit::find_function(amc::Hash128 signature) {
    if (signature.lo == 0 && signature.hi == 0) return nullptr;
    for (const auto &function : _m.functions)
        if (function.signature == signature) return &function;
    return nullptr;
}

std::string Emit::self_type_of(
    const amc::Function *create, const amc::Function *destroy, const amc::ir::LoweredExposedType &record) {
    if (create != nullptr) return cpp_type_of(_m, create->return_type);
    if (destroy != nullptr && !destroy->parameters.empty()) return cpp_type_of(_m, destroy->parameters[0].type_id);
    for (const auto &method : record.methods) {
        const amc::Function *function = find_function(method.abi);
        if (function != nullptr && !function->parameters.empty())
            return cpp_type_of(_m, function->parameters[0].type_id);
    }
    return "void*";
}

void Emit::decl(const amc::Function *function) {
    if (function == nullptr) return;
    std::string params;
    for (size_t i = 0; i < function->parameters.size(); ++i) {
        const auto &parameter = function->parameters[i];
        if (i != 0) params += ", ";
        params += cpp_type_of(_m, parameter.type_id);
    }
    _o.print("{} {}({});\n", cpp_type_of(_m, function->return_type), function->name, params);
}

void Emit::handle(const amc::ir::LoweredExposedType &record, const amc::Function *create, const amc::Function *destroy,
    const std::string &self_type) {
    const std::string handle = cpp_name(record.name) + "Handle";

    _o.print("\nextern \"C\" {{\n");
    decl(create);
    decl(destroy);
    for (const auto &method : record.methods)
        decl(find_function(method.abi));
    _o.print("}}\n");
    _o.print(
        "\n"
        "// RAII handle for the exposed `{0}` capability.\n"
        "class {1} {{\npublic:\n"
        "    {1}() = default;\n"
        "    explicit {1}({2} ptr) : _ptr(ptr) {{}}\n"
        "    {1}(const {1} &) = delete;\n"
        "    {1} &operator=(const {1} &) = delete;\n"
        "    {1}({1} &&other) noexcept : _ptr(other._ptr) {{ other._ptr = nullptr; }}\n"
        "    {1} &operator=({1} &&other) noexcept {{\n"
        "        if (this != &other) {{ reset(); _ptr = other._ptr; other._ptr = nullptr; }}\n"
        "        return *this;\n    }}\n"
        "    ~{1}() {{ reset(); }}\n",
        record.name, handle, self_type);
    if (destroy != nullptr) {
        _o.print(
            "    void reset() {{"
            "        if (_ptr != nullptr) {{ {}(_ptr); _ptr = nullptr; }}\n"
            "    }}\n",
            destroy->name, handle, handle);
    } else
        _o.print("    void reset() {{ _ptr = nullptr; }}\n");

    _o.print("    {} native_handle() const {{ return _ptr; }}\n", self_type);
    _o.print("    explicit operator bool() const {{ return _ptr != nullptr; }}\n");
    if (create != nullptr) {
        std::string params, args;
        for (size_t i = 0; i < create->parameters.size(); ++i) {
            const auto &parameter = create->parameters[i];
            const std::string name = parameter.name.empty() ? fmt::format("arg{}", i) : cpp_name(parameter.name);
            if (i != 0) {
                params += ", ";
                args += ", ";
            }
            params += cpp_type_of(_m, parameter.type_id) + " " + name;
            args += name;
        }
        _o.print("    static {0} create({1}) {{ return {0}({2}({3})); }}\n", handle, params, create->name, args);
    }
    for (const auto &method : record.methods) {
        const amc::Function *function = find_function(method.abi);
        if (function == nullptr || function->parameters.empty()) continue;
        const std::string name = cpp_name(method.name);
        if (name == "create" || name == "reset" || name == "native_handle") continue;
        const int return_index = [&] {
            for (size_t i = 0; i < _m.types.size(); ++i)
                if (_m.types[i].id == function->return_type) return static_cast<int>(i);
            return -1;
        }();
        const bool returns_void = return_index >= 0
                               && _m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                               && (_m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                      == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
        std::string params, args;
        for (size_t i = 1; i < function->parameters.size(); ++i) {
            const auto &parameter = function->parameters[i];
            const std::string parameter_name =
                parameter.name.empty() ? fmt::format("arg{}", i - 1) : cpp_name(parameter.name);
            if (i != 1) params += ", ";
            params += cpp_type_of(_m, parameter.type_id) + " " + parameter_name;
            args += (i == 1 ? "" : ", ") + parameter_name;
        }
        if (returns_void)
            _o.print("    void {}({}) const {{ {}(_ptr{}); }}\n", name, params, function->name,
                args.empty() ? "" : ", " + args);
        else
            _o.print("    {} {}({}) const {{ return {}(_ptr{}); }}\n", cpp_type_of(_m, function->return_type), name,
                params, function->name, args.empty() ? "" : ", " + args);
    }
    _o.print(
        "private:\n"
        "    {} _ptr = nullptr;\n"
        "}};\n",
        self_type);
}

void Emit::handles(const amc::ir::LoweringResult &lowering) {
    // Exposed-handle wrappers: a move-only RAII class around the create/destroy
    // thunks plus typed method calls. The method list and signatures come from
    // the lowering contract, never from parsing the thunk names.
    for (const auto &record : lowering.exposed) {
        if (record.status != amc::ir::LoweringStatus::lowered) continue;
        const amc::Function *create = find_function(record.create);
        const amc::Function *destroy = find_function(record.destroy);
        if (create == nullptr && destroy == nullptr && record.methods.empty()) continue;
        handle(record, create, destroy, self_type_of(create, destroy, record));
    }
}

}   // namespace amc::lang::backend::cpp
