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
#include "SKL/AMC/Backend/Codegen.h"
#include "SKL/AMC/Backend/Zig/Zig.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Lang/Lang.h"

#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Artifact/Metadata.h"

#include <algorithm>
#include <cctype>
#include <stdint.h>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

namespace amc::lang::zig {
namespace {

// Thin wrapper over `fmt::memory_buffer` so the emit helpers read like a file
// stream while the projection is built in memory.
struct Output {
    fmt::memory_buffer &buffer;
    template<typename... Args>
    void print(fmt::format_string<Args...> format, Args &&...args) {
        fmt::format_to(std::back_inserter(buffer), format, std::forward<Args>(args)...);
    }
};

bool is_zig_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool is_zig_keyword(const std::string &s) {
    static const char * const keywords[] = {"addrspace", "align", "allowzero", "and", "anyframe", "anytype", "asm",
        "async", "await", "break", "callconv", "catch", "comptime", "const", "continue", "defer", "else", "enum",
        "errdefer", "error", "export", "extern", "fn", "for", "if", "inline", "linksection", "noalias", "noinline",
        "nosuspend", "opaque", "or", "orelse", "packed", "pub", "resume", "return", "struct", "suspend", "switch",
        "test", "threadlocal", "try", "union", "unreachable", "usingnamespace", "var", "volatile", "while"};
    for (const char *k : keywords)
        if (s == k) return true;
    return false;
}

// A legal Zig identifier, or a quoted `@"..."` form when the ABI name is not
// directly usable (namespaced names, keywords, ...).
std::string zig_ident(const std::string &name) {
    if (is_zig_identifier(name) && !is_zig_keyword(name)) return name;
    std::string out = "@\"";
    for (const char c : name) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string zig_string(const std::string &value) { return amc::backend::escape_string_literal(value); }

// Reduce a type spelling to its bare name so a pointer's pointee can be matched
// against the value types in the module (same approach as the Rust backend).
std::string normalise_type_spelling(const std::string &spelling) {
    static const char * const qualifiers[] = {"const", "volatile", "struct", "class", "enum", "union"};
    std::string out;
    std::string token;
    const auto flush = [&]() {
        if (token.empty()) return;
        bool qualifier = false;
        for (const char *q : qualifiers)
            if (token == q) {
                qualifier = true;
                break;
            }
        if (!qualifier) out += token;
        token.clear();
    };
    for (const char c : spelling) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '*' || c == '&') {
            flush();
            continue;
        }
        token += c;
    }
    flush();
    return out;
}

bool spelling_has_qualifier(const std::string &spelling, const std::string &qualifier) {
    std::string token;
    for (size_t i = 0; i <= spelling.size(); ++i) {
        const char c = i < spelling.size() ? spelling[i] : ' ';
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (token == qualifier) return true;
            token.clear();
        } else {
            token += c;
        }
    }
    return false;
}

class Projector {
public:
    explicit Projector(const amc::AbiModule &module, const amc::ir::LoweringResult *lowering)
        : m(module)
        , lowering(lowering) {
        names.reserve(m.types.size());
        std::unordered_map<std::string, uint32_t> used;
        for (const auto &type : m.types) {
            std::string candidate = type.name;
            const auto count = used[candidate]++;
            if (count != 0) candidate += fmt::format("_{}", count);
            names.push_back(std::move(candidate));
        }
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].kind != amc::TypeKind::pointer)
                by_spelling.emplace(normalise_type_spelling(m.types[i].name), static_cast<int>(i));
    }

    int index_of(amc::Hash128 id) const {
        if (lowering != nullptr)
            for (const auto &type : lowering->types)
                if (type.abi == id && type.abi_index < m.types.size() && m.types[type.abi_index].id == id)
                    return static_cast<int>(type.abi_index);
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].id == id) return static_cast<int>(i);
        return -1;
    }

    const std::string &raw_name(amc::Hash128 id) const {
        const int index = index_of(id);
        return index < 0 ? empty : names[static_cast<size_t>(index)];
    }

    std::string decl_name(amc::Hash128 id) const { return zig_ident(raw_name(id)); }

    std::string field_type(amc::Hash128 id) const {
        const int index = index_of(id);
        if (index < 0) return "anyopaque";
        const auto &type = m.types[static_cast<size_t>(index)];
        if (type.kind == amc::TypeKind::primitive) return primitive_zig_type(type.primitive_abi);
        if (type.kind == amc::TypeKind::pointer) return pointer_zig_type(type);
        return decl_name(id);
    }

    std::string pointer_zig_type(const amc::Type &type) const {
        const std::string &n = type.name;
        bool is_const = false;
        std::string pointee;
        if (!n.empty() && n.back() == '*') {
            pointee = n.substr(0, n.size() - 1);
            is_const = spelling_has_qualifier(pointee, "const");
        } else if (n.rfind("*const", 0) == 0) {
            is_const = true;
            pointee = n.substr(6);
        } else if (n.rfind("*mut", 0) == 0) {
            pointee = n.substr(4);
        } else if (!n.empty() && n[0] == '&') {
            pointee = n.rfind("&mut", 0) == 0 ? n.substr(4) : n.substr(1);
            is_const = n.rfind("&mut", 0) != 0;
        } else {
            return "?*anyopaque";
        }
        if (pointee.find('*') != std::string::npos || pointee.find('&') != std::string::npos)
            return is_const ? "?*const anyopaque" : "?*anyopaque";
        const std::string base = resolve_spelling(pointee);
        // Zig 0.16 rejects `[*c]anyopaque`; opaque pointees use a nullable pointer.
        if (base.empty() || base == "void" || base == "anyopaque")
            return is_const ? "?*const anyopaque" : "?*anyopaque";
        return (is_const ? "[*c]const " : "[*c]") + base;
    }

    const amc::ir::LoweredExposedType *exposed_for(amc::Hash128 type_id) const {
        if (lowering == nullptr) return nullptr;
        for (const auto &record : lowering->exposed)
            if (record.abi == type_id && record.status == amc::ir::LoweringStatus::lowered) return &record;
        return nullptr;
    }

    const std::vector<amc::ir::LoweredExposedType> &exposed_types() const {
        static const std::vector<amc::ir::LoweredExposedType> empty;
        return lowering != nullptr ? lowering->exposed : empty;
    }

    const amc::Function *function_by_signature(amc::Hash128 signature) const {
        if (signature.lo == 0 && signature.hi == 0) return nullptr;
        if (lowering != nullptr)
            for (const auto &function : lowering->functions)
                if (function.abi == signature && function.abi_index < m.functions.size())
                    return &m.functions[function.abi_index];
        for (const auto &function : m.functions)
            if (function.signature == signature) return &function;
        return nullptr;
    }

    const amc::AbiModule &m;

private:
    std::string resolve_spelling(const std::string &spelling) const {
        const auto found = by_spelling.find(normalise_type_spelling(spelling));
        if (found == by_spelling.end()) return {};
        return field_type(m.types[static_cast<size_t>(found->second)].id);
    }

    const amc::ir::LoweringResult *lowering = nullptr;
    std::vector<std::string> names;
    std::unordered_map<std::string, int> by_spelling;
    std::string empty;
};

std::string enum_underlying(uint32_t size) {
    switch (size) {
        case 1:  return "i8";
        case 2:  return "i16";
        case 8:  return "i64";
        case 16: return "i128";
        case 4:
        default: return "c_int";
    }
}

void emit_constants(Output &o, const Projector &p, const amc::Type &type) {
    const std::string name = p.decl_name(type.id);
    o.print("pub const {}_ABIX_TYPE_ID_LO: u64 = 0x{:x};\n", p.raw_name(type.id), type.id.lo);
    o.print("pub const {}_ABIX_TYPE_ID_HI: u64 = 0x{:x};\n", p.raw_name(type.id), type.id.hi);
    o.print("pub const {}_ABIX_SIZE: usize = {};\n", p.raw_name(type.id), type.size);
    o.print("pub const {}_ABIX_ALIGN: usize = {};\n", p.raw_name(type.id), type.align);
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name.empty()) continue;
        o.print("pub const {}_ABIX_{}_OFFSET: usize = {};\n", p.raw_name(type.id), field.name, field.offset);
    }
}

void emit_record(Output &o, const Projector &p, const amc::Type &type) {
    if (type.field_count == 0) {
        o.print("pub const {} = extern struct {{\n", p.decl_name(type.id));
        o.print("    bytes: [{}]u8 align({}),\n", type.size == 0 ? 1 : type.size, type.align == 0 ? 1 : type.align);
        o.print("}};\n");
        emit_constants(o, p, type);
        return;
    }
    uint32_t natural = 1;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const int index = p.index_of(p.m.fields[type.field_begin + i].type_id);
        if (index >= 0) natural = std::max(natural, p.m.types[static_cast<size_t>(index)].align);
    }
    o.print("pub const {} = extern struct {{\n", p.decl_name(type.id));
    uint32_t base_index = 0;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if ((field.flags & amc::field_base) != 0) {
            o.print("    __base{}: {},\n", base_index++, p.field_type(field.type_id));
            continue;
        }
        if (field.name.empty()) continue;
        o.print("    {}: {},\n", zig_ident(field.name), p.field_type(field.type_id));
    }
    if (type.align > natural)
        o.print("}} align({});\n", type.align);
    else
        o.print("}};\n");
    emit_constants(o, p, type);
}

void emit_enumeration(Output &o, const Projector &p, const amc::Type &type) {
    o.print("pub const {} = {};\n", p.decl_name(type.id), enum_underlying(type.size));
}

void emit_alias(Output &o, const Projector &p, const amc::Type &type) {
    std::string underlying = "anyopaque";
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name == "underlying") {
            underlying = p.field_type(field.type_id);
            break;
        }
    }
    o.print("pub const {} = {};\n", p.decl_name(type.id), underlying);
}

void emit_opaque(Output &o, const Projector &p, const amc::Type &type) {
    o.print("pub const {} = extern struct {{\n", p.decl_name(type.id));
    o.print("    bytes: [{}]u8 align({}),\n", type.size, type.align == 0 ? 1 : type.align);
    o.print("}};\n");
    emit_constants(o, p, type);
}

void emit_type(Output &o, const Projector &p, const amc::Type &type) {
    switch (type.kind) {
        case amc::TypeKind::record:
            if ((type.flags & amc::type_opaque) != 0)
                emit_opaque(o, p, type);
            else
                emit_record(o, p, type);
            break;
        case amc::TypeKind::enumeration: emit_enumeration(o, p, type); break;
        case amc::TypeKind::alias:       emit_alias(o, p, type); break;
        case amc::TypeKind::pointer:
            o.print("pub const {} = {};\n", p.decl_name(type.id), p.pointer_zig_type(type));
            break;
        case amc::TypeKind::array:          emit_opaque(o, p, type); break;
        case amc::TypeKind::namespace_type: break;
        case amc::TypeKind::primitive:      break;
        case amc::TypeKind::function:       break;
    }
}

void emit_functions(Output &o, const Projector &p) {
    std::vector<const amc::Function *> free_functions;
    for (const auto &function : p.m.functions)
        if (function.owner_type.lo == 0 && function.owner_type.hi == 0) free_functions.push_back(&function);
    if (free_functions.empty()) return;
    o.print("\n// Free functions declared with the C calling convention.\n");
    for (const auto *function : free_functions) {
        std::string params;
        for (size_t i = 0; i < function->parameters.size(); ++i) {
            if (i != 0) params += ", ";
            const auto &parameter = function->parameters[i];
            std::string parameter_name = parameter.name.empty() ? fmt::format("arg{}", i) : zig_ident(parameter.name);
            params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
        }
        const int return_index = p.index_of(function->return_type);
        const bool returns_void = return_index >= 0
                               && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                               && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                      == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
        if (returns_void)
            o.print("pub extern fn {}({}) void;\n", zig_ident(function->name), params);
        else
            o.print(
                "pub extern fn {}({}) {};\n", zig_ident(function->name), params, p.field_type(function->return_type));
    }
}

void emit_exposed_handles(Output &o, const Projector &p) {
    const auto &records = p.exposed_types();
    if (records.empty()) return;
    o.print("\n// Exposed-handle wrappers: create/destroy and typed method thunks.\n");
    for (const auto &record : records) {
        if (record.status != amc::ir::LoweringStatus::lowered) continue;
        const amc::Function *create = p.function_by_signature(record.create);
        const amc::Function *destroy = p.function_by_signature(record.destroy);
        const std::string handle = p.decl_name(record.abi) + "Handle";
        std::string self_type = "?*anyopaque";
        if (create != nullptr)
            self_type = p.field_type(create->return_type);
        else if (destroy != nullptr && !destroy->parameters.empty())
            self_type = p.field_type(destroy->parameters[0].type_id);
        else
            for (const auto &method : record.methods) {
                const amc::Function *function = p.function_by_signature(method.abi);
                if (function != nullptr && !function->parameters.empty()) {
                    self_type = p.field_type(function->parameters[0].type_id);
                    break;
                }
            }
        o.print("pub const {} = struct {{\n", handle);
        o.print("    ptr: {},\n", self_type);

        if (create != nullptr) {
            std::string params, args;
            for (size_t i = 0; i < create->parameters.size(); ++i) {
                const auto &parameter = create->parameters[i];
                const std::string name = parameter.name.empty() ? fmt::format("arg{}", i) : zig_ident(parameter.name);
                params += fmt::format("{}{}: {}", i == 0 ? "" : ", ", name, p.field_type(parameter.type_id));
                args += fmt::format("{}{}", i == 0 ? "" : ", ", name);
            }
            o.print("    pub fn create({}) ?{} {{\n", params, handle);
            o.print("        const raw = {}({});\n", zig_ident(create->name), args);
            o.print("        if (raw == null) return null;\n");
            o.print("        return .{{ .ptr = raw }};\n");
            o.print("    }}\n");
        }
        if (destroy != nullptr) {
            o.print("    pub fn deinit(self: *{}) void {{\n", handle);
            o.print("        if (self.ptr != null) {{\n");
            o.print("            {}(self.ptr);\n", zig_ident(destroy->name));
            o.print("            self.ptr = null;\n");
            o.print("        }}\n");
            o.print("    }}\n");
        }
        for (const auto &method : record.methods) {
            const amc::Function *function = p.function_by_signature(method.abi);
            if (function == nullptr || function->parameters.empty()) continue;
            const std::string name = zig_ident(method.name);
            if (name == "create" || name == "deinit") continue;
            std::string params, args;
            for (size_t i = 1; i < function->parameters.size(); ++i) {
                const auto &parameter = function->parameters[i];
                const std::string parameter_name =
                    parameter.name.empty() ? fmt::format("arg{}", i - 1) : zig_ident(parameter.name);
                params += fmt::format(", {}: {}", parameter_name, p.field_type(parameter.type_id));
                args += fmt::format(", {}", parameter_name);
            }
            const int return_index = p.index_of(function->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
            if (returns_void)
                o.print("    pub fn {}(self: *{}{}) void {{ {}(self.ptr{}); }}\n", name, handle, params,
                    zig_ident(function->name), args);
            else
                o.print("    pub fn {}(self: *{}{}) {} {{ return {}(self.ptr{}); }}\n", name, handle, params,
                    p.field_type(function->return_type), zig_ident(function->name), args);
        }
        o.print("}};\n");
    }
}

void emit_capability_contracts(Output &o, const Projector &p) {
    if (p.m.interfaces.empty()) return;
    o.print("\n// Interface capability contracts: identity, version, slots and signatures.\n");
    o.print(
        "pub const CapabilityMethod = struct {{\n    name: []const u8,\n    slot: u32,\n"
        "    signature_lo: u64,\n    signature_hi: u64,\n}};\n");
    o.print(
        "pub const CapabilityContract = struct {{\n    name: []const u8,\n    id_lo: u64,\n    id_hi: u64,\n"
        "    version: u32,\n    methods: []const CapabilityMethod,\n}};\n");
    std::vector<std::string> names;
    for (const auto &record : p.m.interfaces) {
        std::vector<const amc::InterfaceMethod *> by_slot;
        for (uint32_t i = 0; i < record.method_count; ++i) {
            const auto &method = p.m.interface_methods[record.method_begin + i];
            if (method.slot >= by_slot.size()) by_slot.resize(method.slot + 1, nullptr);
            by_slot[method.slot] = &method;
        }
        const std::string constant = zig_ident(record.name) + "_CONTRACT";
        names.push_back(constant);
        o.print("pub const {} = CapabilityContract{{\n", constant);
        o.print("    .name = \"{}\",\n    .id_lo = 0x{:x},\n    .id_hi = 0x{:x},\n    .version = {},\n", record.name,
            record.id.lo, record.id.hi, record.version);
        o.print("    .methods = &[_]CapabilityMethod{{\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            o.print("        .{{ .name = \"{}\", .slot = {}, .signature_lo = 0x{:x}, .signature_hi = 0x{:x} }},\n",
                method->name, method->slot, method->signature.lo, method->signature.hi);
        }
        o.print("    }},\n}};\n");
    }
    o.print("pub const CAPABILITIES = [_]CapabilityContract{{\n");
    for (const auto &name : names)
        o.print("    {},\n", name);
    o.print("}};\n");
}

void emit_interface_proxies(Output &o, const Projector &p) {
    if (p.m.interfaces.empty()) return;
    o.print("\n// Interface proxies: bind-time version/slot/signature validation.\n");
    o.print(
        "pub const InterfaceMethodEntry = extern struct {{\n    signature_lo: u64,\n    signature_hi: u64,\n"
        "    entry: usize,\n    slot: u32,\n    flags: u32,\n}};\n");
    o.print(
        "pub const InterfaceBindError = error{{\n    InterfaceNotFound,\n    VersionMismatch,\n    MethodMissing,\n"
        "    SignatureMismatch,\n}};\n");
    o.print("/// Opaque module/loader lease held by an interface proxy.\n");
    o.print(
        "pub const InterfaceLease = struct {{\n    raw: ?*anyopaque,\n"
        "    release: *const fn (?*anyopaque) callconv(.c) void,\n");
    o.print(
        "    pub fn init(raw: ?*anyopaque, release: *const fn (?*anyopaque) callconv(.c) void) InterfaceLease {{\n"
        "        return .{{ .raw = raw, .release = release }};\n    }}\n");
    o.print(
        "    pub fn deinit(self: *InterfaceLease) void {{\n"
        "        if (self.raw) |raw| self.release(raw);\n        self.raw = null;\n    }}\n}};\n");
    for (const auto &record : p.m.interfaces) {
        std::vector<const amc::InterfaceMethod *> by_slot;
        for (uint32_t i = 0; i < record.method_count; ++i) {
            const auto &method = p.m.interface_methods[record.method_begin + i];
            if (method.slot >= by_slot.size()) by_slot.resize(method.slot + 1, nullptr);
            by_slot[method.slot] = &method;
        }
        const std::string type = zig_ident(record.name) + "Proxy";
        o.print("\npub const {} = struct {{\n    object: *anyopaque,\n", type);
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            std::string params = "?*anyopaque";
            for (size_t i = method->parameters.empty() ? 0 : 1; i < method->parameters.size(); ++i)
                params += ", " + p.field_type(method->parameters[i].type_id);
            const int return_index = p.index_of(method->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
            o.print("    {}: *const fn ({}) callconv(.c) {},\n", zig_ident("fn_" + method->name), params,
                returns_void ? "void" : p.field_type(method->return_type));
        }
        o.print("    lease: ?InterfaceLease,\n");
        o.print("    pub const ID_LO: u64 = 0x{:x};\n    pub const ID_HI: u64 = 0x{:x};\n", record.id.lo, record.id.hi);
        o.print("    pub const VERSION: u32 = {};\n", record.version);
        o.print("    const SLOT_CAPACITY: usize = {};\n", by_slot.size());
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            const std::string name = zig_ident(method->name);
            o.print("    pub const {}_slot: u32 = {};\n", zig_ident(name), method->slot);
            o.print("    pub const {}_signature_lo: u64 = 0x{:x};\n", zig_ident(name), method->signature.lo);
            o.print("    pub const {}_signature_hi: u64 = 0x{:x};\n", zig_ident(name), method->signature.hi);
        }
        o.print(
            "    pub fn bind(interface_id_lo: u64, interface_id_hi: u64, version: u32, object: *anyopaque,\n"
            "        methods: []const InterfaceMethodEntry, lease: ?InterfaceLease) InterfaceBindError!{} {{\n",
            type);
        o.print("        if (interface_id_lo != ID_LO or interface_id_hi != ID_HI) return error.InterfaceNotFound;\n");
        o.print("        if (version != VERSION) return error.VersionMismatch;\n");
        o.print("        var entries: [SLOT_CAPACITY]usize = undefined;\n");
        o.print("        var signatures_lo: [SLOT_CAPACITY]u64 = undefined;\n");
        o.print("        var signatures_hi: [SLOT_CAPACITY]u64 = undefined;\n");
        o.print("        @memset(entries[0..], 0);\n");
        o.print("        @memset(signatures_lo[0..], 0);\n");
        o.print("        @memset(signatures_hi[0..], 0);\n");
        o.print("        for (methods) |method| {{\n");
        o.print("            if (method.entry == 0 or method.slot >= SLOT_CAPACITY) continue;\n");
        o.print("            entries[method.slot] = method.entry;\n");
        o.print("            signatures_lo[method.slot] = method.signature_lo;\n");
        o.print("            signatures_hi[method.slot] = method.signature_hi;\n        }}\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            const std::string name = zig_ident(method->name);
            const std::string slot = zig_ident(name + "_slot");
            const std::string sig_lo = zig_ident(name + "_signature_lo");
            const std::string sig_hi = zig_ident(name + "_signature_hi");
            o.print("        if (entries[{}] == 0) return error.MethodMissing;\n", slot);
            o.print("        if (signatures_lo[{}] != {} or signatures_hi[{}] != {}) return error.SignatureMismatch;\n",
                slot, sig_lo, slot, sig_hi);
        }
        o.print("        return .{{\n            .object = object,\n            .lease = lease,\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            const std::string field = zig_ident("fn_" + method->name);
            std::string params = "?*anyopaque";
            for (size_t i = method->parameters.empty() ? 0 : 1; i < method->parameters.size(); ++i)
                params += ", " + p.field_type(method->parameters[i].type_id);
            const int return_index = p.index_of(method->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
            o.print(
                "            .{} = @as(*const fn ({}) callconv(.c) {}, @ptrCast(@alignCast(@as(*anyopaque, "
                "@ptrFromInt(entries[{}]))))),\n",
                field, params, returns_void ? "void" : p.field_type(method->return_type),
                zig_ident(zig_ident(method->name) + "_slot"));
        }
        o.print("        }};\n    }}\n");
        o.print(
            "    pub fn deinit(self: *{}) void {{\n        if (self.lease) |*lease| lease.deinit();\n    }}\n", type);
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            const std::string name = zig_ident(method->name);
            std::string params, args;
            for (size_t i = method->parameters.empty() ? 0 : 1; i < method->parameters.size(); ++i) {
                if (!params.empty()) {
                    params += ", ";
                    args += ", ";
                }
                const std::string arg = "arg" + std::to_string(i - (method->parameters.empty() ? 0 : 1));
                params += arg + ": " + p.field_type(method->parameters[i].type_id);
                args += arg;
            }
            const int return_index = p.index_of(method->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
            if (returns_void)
                o.print("    pub fn {}(self: *const {}{}) void {{ self.{} (self.object{}); }}\n", name, type,
                    params.empty() ? "" : ", " + params, zig_ident("fn_" + method->name),
                    args.empty() ? "" : ", " + args);
            else
                o.print("    pub fn {}(self: *const {}{}) {} {{ return self.{} (self.object{}); }}\n", name, type,
                    params.empty() ? "" : ", " + params, p.field_type(method->return_type),
                    zig_ident("fn_" + method->name), args.empty() ? "" : ", " + args);
        }
        o.print("}};\n");
    }
}

void emit_error_sets(Output &o, const Projector &p) {
    if (p.m.error_sets.empty()) return;
    o.print("\n// Named error sets: stable numeric values, names and conditions.\n");
    for (const auto &set : p.m.error_sets) {
        const std::string name = zig_ident(set.name);
        o.print("pub const {} = struct {{\n", name);
        o.print("    pub const domain: u64 = 0x{:x};\n", static_cast<unsigned long long>(set.domain));
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = p.m.error_codes[set.code_begin + i];
            o.print("    pub const {}: c_int = {};\n", zig_ident(code.name), code.value);
        }
        o.print("    pub fn name(value: c_int) []const u8 {{\n        return switch (value) {{\n");
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = p.m.error_codes[set.code_begin + i];
            o.print("            {} => \"{}\",\n", code.value, code.name);
        }
        o.print("            else => \"unknown\",\n        }};\n    }}\n");
        o.print("    pub fn condition(value: c_int) i32 {{\n        return switch (value) {{\n");
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = p.m.error_codes[set.code_begin + i];
            o.print("            {} => {},\n", code.value, code.condition);
        }
        o.print("            else => -1,\n        }};\n    }}\n");
        o.print("    pub fn condition_name(value: c_int) []const u8 {{\n        return switch (condition(value)) {{\n");
        std::set<int32_t> conditions;
        for (uint32_t i = 0; i < set.code_count; ++i)
            conditions.insert(p.m.error_codes[set.code_begin + i].condition);
        for (const int32_t condition : conditions)
            o.print("            {} => \"{}\",\n", condition,
                amc::generic_condition_name(static_cast<amc::GenericCondition>(condition)));
        o.print("            else => \"unknown\",\n        }};\n    }}\n}};\n");
    }
}

void emit_error_wrappers(Output &o, const Projector &p) {
    if (p.m.errors.empty()) return;
    o.print("\n// Typed error boundary: a declared C status becomes a Result union.\n");
    o.print("pub fn AmcResult(comptime T: type, comptime E: type) type {{\n");
    o.print("    return union(enum) {{ ok: T, err: E }};\n");
    o.print("}}\n");
    for (const auto &record : p.m.errors) {
        if (record.function_index >= p.m.functions.size()) continue;
        const amc::Function &function = p.m.functions[record.function_index];
        const auto find_type = [&](amc::Hash128 id) -> const amc::Type * {
            for (const auto &type : p.m.types)
                if (type.id == id) return &type;
            return nullptr;
        };
        const auto pointee_of = [&](amc::Hash128 id) -> amc::Hash128 {
            for (const auto &ref : p.m.pointees)
                if (ref.pointer == id) return ref.pointee;
            return {};
        };
        const auto parameter_name = [](const amc::Parameter &parameter, size_t index) {
            return parameter.name.empty() ? fmt::format("arg{}", index) : zig_ident(parameter.name);
        };

        std::string value_type, status_type;
        std::string status_access, value_access;
        std::string params, call_args;
        if (record.status_source == amc::StatusSource::return_field) {
            const amc::Type *result = find_type(function.return_type);
            if (result == nullptr
                || record.status_parameter >= result->field_count
                || record.value_parameter >= result->field_count)
                continue;
            const auto &status_field = p.m.fields[result->field_begin + record.status_parameter];
            const auto &value_field = p.m.fields[result->field_begin + record.value_parameter];
            value_type = p.field_type(value_field.type_id);
            status_type = p.field_type(status_field.type_id);
            status_access = zig_ident(status_field.name);
            value_access = zig_ident(value_field.name);
            for (size_t i = 0; i < function.parameters.size(); ++i) {
                if (i != 0) {
                    params += ", ";
                    call_args += ", ";
                }
                const std::string name = parameter_name(function.parameters[i], i);
                params += fmt::format("{}: {}", name, p.field_type(function.parameters[i].type_id));
                call_args += name;
            }
        } else if (record.status_source == amc::StatusSource::return_value) {
            if (record.value_parameter == UINT32_MAX || record.value_parameter >= function.parameters.size()) continue;
            const amc::Hash128 value_id = pointee_of(function.parameters[record.value_parameter].type_id);
            if (value_id.lo == 0 && value_id.hi == 0) continue;
            value_type = p.field_type(value_id);
            status_type = p.field_type(function.return_type);
            for (size_t i = 0; i < function.parameters.size(); ++i) {
                if (i == record.value_parameter) {
                    call_args += (call_args.empty() ? "" : ", ") + std::string("&value");
                    continue;
                }
                if (!params.empty()) {
                    params += ", ";
                    call_args += ", ";
                }
                const std::string name = parameter_name(function.parameters[i], i);
                params += fmt::format("{}: {}", name, p.field_type(function.parameters[i].type_id));
                call_args += name;
            }
        } else {
            continue;
        }

        o.print(
            "pub fn {}_result({}) AmcResult({}, {}) {{\n", zig_ident(function.name), params, value_type, status_type);
        if (record.status_source == amc::StatusSource::return_value)
            o.print("    var value: {} = undefined;\n", value_type);
        if (record.status_source == amc::StatusSource::return_field)
            o.print("    const raw = {}({});\n", zig_ident(function.name), call_args);
        else
            o.print("    const status = {}({});\n", zig_ident(function.name), call_args);
        if (record.status_source == amc::StatusSource::return_field)
            o.print("    if (raw.{} == @as({}, {})) return .{{ .ok = raw.{} }};\n", status_access, status_type,
                record.success_value, value_access);
        else
            o.print("    if (status == @as({}, {})) return .{{ .ok = value }};\n", status_type, record.success_value);
        if (record.status_source == amc::StatusSource::return_field)
            o.print("    return .{{ .err = raw.{} }};\n", status_access);
        else
            o.print("    return .{{ .err = status }};\n");
        o.print("}}\n");
    }
}

void emit_layout_checks(Output &o, const Projector &p) {
    bool any = false;
    for (const auto &type : p.m.types) {
        if (type.kind != amc::TypeKind::record || type.name.empty()) continue;
        if (!any) {
            o.print("\n// Compile-time ABI assertions: a layout drift is a compile error.\n");
            o.print("comptime {{\n");
            any = true;
        }
        const std::string name = p.decl_name(type.id);
        o.print("    if (@sizeOf({}) != {}) @compileError(\"{} size mismatch\");\n", name, type.size, type.name);
        o.print("    if (@alignOf({}) != {}) @compileError(\"{} align mismatch\");\n", name,
            type.align == 0 ? 1 : type.align, type.name);
        if ((type.flags & amc::type_opaque) != 0) continue;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = p.m.fields[type.field_begin + i];
            if (field.name.empty() || (field.flags & amc::field_base) != 0) continue;
            if ((field.flags & amc::field_bitfield) != 0) continue;
            o.print("    if (@offsetOf({}, {}) != {}) @compileError(\"{}::{} offset mismatch\");\n", name,
                zig_string(field.name), field.offset, type.name, field.name);
        }
    }
    if (any) o.print("}}\n");
}

bool emit_metadata_region(Output &o, const amc::AbiModule &m, std::string &error) {
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(m, options, region, error)) return false;
    o.print("\n// ABIX Metadata Region: manifest + desc + hash + names (offset-based).\n");
    o.print("pub const AMC_METADATA_REGION: [{}]u8 = .{{\n", region.size());
    for (size_t i = 0; i < region.size(); ++i) {
        if (i % 12 == 0) o.print("    ");
        o.print("0x{:02x},", region[i]);
        if (i % 12 == 11 || i + 1 == region.size()) o.print("\n");
    }
    o.print("}};\n");
    o.print("pub const AMC_METADATA_REGION_SIZE: usize = {};\n", region.size());
    return true;
}

}   // namespace

std::string primitive_zig_type(uint32_t primitive_abi) {
    const uint32_t kind = primitive_abi & 0xffu;
    const uint32_t width = (primitive_abi & amc::primitive_width_mask) >> amc::primitive_width_shift;
    switch (static_cast<amc::PrimitiveAbiKind>(kind)) {
        case amc::PrimitiveAbiKind::void_:          return "void";
        case amc::PrimitiveAbiKind::bool_:          return "bool";
        case amc::PrimitiveAbiKind::char_signed:
        case amc::PrimitiveAbiKind::char_unsigned:  return "c_char";
        case amc::PrimitiveAbiKind::schar:          return "i8";
        case amc::PrimitiveAbiKind::uchar:          return "u8";
        case amc::PrimitiveAbiKind::char8:          return "u8";
        case amc::PrimitiveAbiKind::char16:         return "u16";
        case amc::PrimitiveAbiKind::char32:         return "u32";
        case amc::PrimitiveAbiKind::wchar_signed:   return width == 2 ? "i16" : "i32";
        case amc::PrimitiveAbiKind::wchar_unsigned: return width == 2 ? "u16" : "u32";
        case amc::PrimitiveAbiKind::sint:
            switch (width) {
                case 1:  return "i8";
                case 2:  return "i16";
                case 8:  return "c_longlong";
                case 16: return "i128";
                case 4:
                default: return "c_int";
            }
        case amc::PrimitiveAbiKind::uint:
            switch (width) {
                case 1:  return "u8";
                case 2:  return "u16";
                case 8:  return "c_ulonglong";
                case 16: return "u128";
                case 4:
                default: return "c_uint";
            }
        case amc::PrimitiveAbiKind::floating: {
            switch (static_cast<amc::FloatFormat>(
                (primitive_abi & amc::primitive_format_mask) >> amc::primitive_format_shift)) {
                case amc::FloatFormat::ieee32:            return "f32";
                case amc::FloatFormat::ieee64:            return "f64";
                case amc::FloatFormat::ieee16:            return "u16";
                case amc::FloatFormat::bf16:              return "u16";
                case amc::FloatFormat::ieee128:           return "u128";
                case amc::FloatFormat::x87_80:            return "u128";
                case amc::FloatFormat::ppc_double_double: return "u128";
                case amc::FloatFormat::none:
                default:                                  return "f64";
            }
        }
        case amc::PrimitiveAbiKind::none:
        default:                          return "anyopaque";
    }
}

std::string generate_projection(const amc::AbiModule &module, const amc::ir::LoweringResult *lowering) {
    Projector projector(module, lowering);
    fmt::memory_buffer buffer;
    Output o{buffer};
    o.print(AMC_GENERATED_FILE_WARNING("//"));
    o.print("// ABI: {} {} (arch={}, os={}, abi={})\n", module.package_name, module.package_version, module.arch,
        module.os, module.target_abi);
    o.print("pub const ABIX_PACKAGE_NAME = {};\n", zig_string(module.package_name));
    o.print("pub const ABIX_PACKAGE_VERSION = {};\n", zig_string(module.package_version));
    o.print("pub const ABIX_LIBRARY_NAME = {};\n", zig_string(module.package_name));

    for (const auto &type : module.types) {
        if (amc::lang::cancellation_requested()) return {};
        o.print("\n");
        emit_type(o, projector, type);
    }
    emit_functions(o, projector);
    emit_exposed_handles(o, projector);
    emit_capability_contracts(o, projector);
    emit_interface_proxies(o, projector);
    emit_error_sets(o, projector);
    emit_error_wrappers(o, projector);
    emit_layout_checks(o, projector);
    std::string error;
    if (!emit_metadata_region(o, module, error)) fmt::print(stderr, "{}\n", error);
    return fmt::to_string(buffer);
}

int run_backend(const char *input, const char *output) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::ir::Module artifact;
    std::string error;
    // Backends project the ABI and lowering contract; the semantic sidecar is
    // not consumed on this path, so skip parsing it.
    amc::ir::LoadOptions load_options;
    load_options.semantic = false;
    if (!amc::ir::load_artifact(input, artifact, error, load_options)) {
        fmt::print(stderr, "{}\n", error);
        return 1;
    }
    amc::AbiModule module = std::move(artifact.abi);
    amc::ir::LoweringResult lowering = std::move(artifact.lowering);
    std::string lowering_error;
    if (!amc::ir::apply_lowering_contracts(lowering, module, lowering_error)) {
        fmt::print(stderr, "{}\n", lowering_error);
        return 1;
    }
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    try {
        const std::string projection = generate_projection(module, &lowering);
        if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
        std::error_code ec;
        llvm::raw_fd_ostream file(output, ec, llvm::sys::fs::OF_Text);
        if (ec) {
            fmt::print(stderr, "cannot write {}: {}\n", output, ec.message());
            return 1;
        }
        file << projection;
        file.close();
        if (file.has_error()) {
            fmt::print(stderr, "cannot write {}: {}\n", output, file.error().message());
            return 1;
        }
    } catch (const std::exception &e) {
        fmt::print(stderr, "cannot write {}: {}\n", output, e.what());
        return 1;
    }
    return 0;
}

}   // namespace amc::lang::zig
