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

#include <set>
#include <string>

namespace skl::amc::lang::backend::cpp {

const amc::Function *Emit::find_function(amc::Hash128 signature) {
    if (signature.lo == 0 && signature.hi == 0) return nullptr;
    for (const auto &function : _m.functions)
        if (function.signature == signature) return &function;
    return nullptr;
}

std::string Emit::self_type_of(
    const amc::Function *create, const amc::Function *destroy, const amc::ir::LoweredExposedType &record) {
    if (create != nullptr) return cpp_type_of(_m, create->return_type);
    if (destroy != nullptr && !destroy->params.empty()) return cpp_type_of(_m, destroy->params[0].type_id);
    for (const auto &method : record.methods) {
        const amc::Function *function = find_function(method.abi);
        if (function != nullptr && !function->params.empty()) return cpp_type_of(_m, function->params[0].type_id);
    }
    return "void*";
}

void Emit::decl(const amc::Function *function) {
    if (function == nullptr) return;
    std::string params;
    for (size_t i = 0; i < function->params.size(); ++i) {
        const auto &parameter = function->params[i];
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
        for (size_t i = 0; i < create->params.size(); ++i) {
            const auto &parameter = create->params[i];
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
        if (function == nullptr || function->params.empty()) continue;
        const std::string name = cpp_name(method.name);
        if (name == "create" || name == "reset" || name == "native_handle") continue;
        const int return_index = [&] {
            for (size_t i = 0; i < _m.types.size(); ++i)
                if (_m.types[i].id == function->return_type) return static_cast<int>(i);
            return -1;
        }();
        const bool returns_void = return_index >= 0
                               && _m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::PRIMITIVE
                               && (_m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                      == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
        std::string params, args;
        for (size_t i = 1; i < function->params.size(); ++i) {
            const auto &parameter = function->params[i];
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
        if (record.status != amc::ir::LoweringStatus::LOWERED) continue;
        const amc::Function *create = find_function(record.create);
        const amc::Function *destroy = find_function(record.destroy);
        if (create == nullptr && destroy == nullptr && record.methods.empty()) continue;
        handle(record, create, destroy, self_type_of(create, destroy, record));
    }
}

namespace {

const amc::Field *visible_field(const amc::ABIModule &module, const amc::Type &type, const char *name) {
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const amc::Field &field = module.fields[type.field_begin + i];
        if ((field.flags & amc::field_base) == 0 && (field.flags & amc::field_bitfield) == 0 && field.name == name)
            return &field;
    }
    return nullptr;
}

std::string suffix_after(const std::string &name, const char *prefix) {
    const std::string p(prefix);
    return name.rfind(p, 0) == 0 ? name.substr(p.size()) : std::string{};
}

}   // namespace

void Emit::owner_views() {
    std::set<std::string> emitted;
    for (const auto &record : _m.lowerings) {
        const auto kind = static_cast<amc::LoweringKind>(record.kind);
        if (kind != amc::LoweringKind::OWNER
            && kind != amc::LoweringKind::OWNED_STRING
            && kind != amc::LoweringKind::OWNED_SPAN)
            continue;
        const amc::Type *type = nullptr;
        for (const auto &candidate : _m.types)
            if (candidate.id == record.type_id) {
                type = &candidate;
                break;
            }
        if (type == nullptr || type->kind != amc::TypeKind::RECORD) continue;
        const std::string raw_name = type->name;
        if (!emitted.emplace(raw_name).second) continue;

        if (kind == amc::LoweringKind::OWNER) {
            _o.print("using {} = ::abix_owner_handle;\n", raw_name);
            const std::string suffix = suffix_after(raw_name, "abix_owner_");
            std::string wrapper_suffix = cpp_name(suffix);
            if (!wrapper_suffix.empty())
                wrapper_suffix[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(wrapper_suffix[0])));
            const std::string wrapper = "OwnerOf" + wrapper_suffix;
            _o.print(
                "\n// ABIX owner handle `{0}`: move-only RAII, released through the\n"
                "// creator-provided destroy entry.\n"
                "class {1} {{\npublic:\n"
                "    {1}() = default;\n"
                "    explicit {1}({0} raw) : _raw(raw) {{}}\n"
                "    {1}(const {1} &) = delete;\n"
                "    {1} &operator=(const {1} &) = delete;\n"
                "    {1}({1} &&other) noexcept : _raw(other._raw) {{ other._raw = {{}}; }}\n"
                "    {1} &operator=({1} &&other) noexcept {{\n"
                "        if (this != &other) {{ reset(); _raw = other._raw; other._raw = {{}}; }}\n"
                "        return *this;\n    }}\n"
                "    ~{1}() {{ reset(); }}\n"
                "    void reset() {{\n"
                "        if (_raw.object != nullptr && (_raw.mode == 1 || _raw.mode == 2)\n"
                "            && _raw.destroy != nullptr)\n"
                "            reinterpret_cast<void (*)(void *)>(_raw.destroy)(_raw.object);\n"
                "        _raw = {{}};\n    }}\n"
                "    void *object() const {{ return _raw.object; }}\n"
                "    {0} native_handle() const {{ return _raw; }}\n"
                "    {0} release() {{ auto raw = _raw; _raw = {{}}; return raw; }}\n"
                "    explicit operator bool() const {{\n"
                "        return _raw.object != nullptr && (_raw.mode == 1 || _raw.mode == 2)\n"
                "            && _raw.destroy != nullptr;\n    }}\n"
                "private:\n    {0} _raw{{}};\n}};\n",
                raw_name, wrapper);
        } else {
            const amc::Field *view = visible_field(_m, *type, "view");
            const amc::Field *owner = visible_field(_m, *type, "owner");
            if (view == nullptr || owner == nullptr) continue;
            const amc::Type *view_type = nullptr;
            for (const auto &candidate : _m.types)
                if (candidate.id == view->type_id) {
                    view_type = &candidate;
                    break;
                }
            if (view_type == nullptr || view_type->kind != amc::TypeKind::RECORD) continue;

            // ABIX v0.4.0 only defines the int32 owned-span instance in its C
            // header. Every other element instance is defined here from the ABI
            // record, erasing the element type to `void` so the projection
            // header still compiles without the producer's native header.
            if (kind == amc::LoweringKind::OWNED_SPAN && raw_name != "abix_owned_span_i32") {
                const std::string view_name = view_type->name;
                if (view_name.rfind("abix_span_", 0) != 0) continue;
                _o.print(
                    "\n// ABIX owned-span instance `{0}`. The concrete element type is\n"
                    "// erased at the projection boundary; recover it with data<T>().\n"
                    "using {1} = ::abix_std::abix_span<void>;\n"
                    "using {0} = ::abix_std::abix_owned_span<void>;\n"
                    "static_assert(sizeof({1}) == {2}, \"ABIX owned span view layout changed\");\n"
                    "static_assert(sizeof({0}) == {3}, \"ABIX owned span layout changed\");\n"
                    "static_assert(alignof({0}) == {4}, \"ABIX owned span alignment changed\");\n"
                    "static_assert(offsetof({0}, owner) == sizeof({1}), \"ABIX owned span layout changed\");\n",
                    raw_name, view_name, view_type->size, type->size, type->align);
            }

            const std::string suffix = kind == amc::LoweringKind::OWNED_STRING
                                         ? suffix_after(raw_name, "abix_owned_str_")
                                         : suffix_after(raw_name, "abix_owned_span_");
            std::string wrapper_suffix = cpp_name(suffix);
            if (!wrapper_suffix.empty())
                wrapper_suffix[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(wrapper_suffix[0])));
            const std::string wrapper =
                (kind == amc::LoweringKind::OWNED_STRING ? "OwnedString" : "OwnedSpan") + wrapper_suffix;
            std::string extra;
            if (kind == amc::LoweringKind::OWNED_SPAN)
                extra =
                    "    template <class T> const T *data() const {\n"
                    "        return reinterpret_cast<const T *>(_raw.view.data);\n"
                    "    }\n"
                    "    uint64_t size() const { return _raw.view.len; }\n";
            _o.print(
                "\n// ABIX owned view `{0}`: borrowed view + owner handle.\n"
                "class {1} {{\npublic:\n"
                "    explicit {1}({0} raw) : _raw(raw) {{}}\n"
                "    {1}(const {1} &) = delete;\n"
                "    {1} &operator=(const {1} &) = delete;\n"
                "    {1}({1} &&other) noexcept : _raw(other._raw) {{ other._raw = {{}}; }}\n"
                "    {1} &operator=({1} &&other) noexcept {{\n"
                "        if (this != &other) {{ reset(); _raw = other._raw; other._raw = {{}}; }}\n"
                "        return *this;\n    }}\n"
                "    ~{1}() {{ reset(); }}\n"
                "    void reset() {{\n"
                "        if (_raw.owner.object != nullptr && (_raw.owner.mode == 1 || _raw.owner.mode == 2)\n"
                "            && _raw.owner.destroy != nullptr)\n"
                "            reinterpret_cast<void (*)(void *)>(_raw.owner.destroy)(_raw.owner.object);\n"
                "        _raw = {{}};\n    }}\n"
                "    const {2} &view() const {{ return _raw.view; }}\n"
                "{3}"
                "    {0} native_handle() const {{ return _raw; }}\n"
                "    {0} release() {{ auto raw = _raw; _raw = {{}}; return raw; }}\n"
                "    explicit operator bool() const {{\n"
                "        return _raw.owner.object != nullptr && (_raw.owner.mode == 1 || _raw.owner.mode == 2)\n"
                "            && _raw.owner.destroy != nullptr;\n    }}\n"
                "private:\n    {0} _raw{{}};\n}};\n",
                raw_name, wrapper, view_type->name, extra);
        }
    }
}

}   // namespace skl::amc::lang::backend::cpp
