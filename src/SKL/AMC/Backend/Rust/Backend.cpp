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
#include "SKL/AMC/Backend/Rust/Rust.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Lang/Lang.h"

#include "SKL/AMC/Backend/CodeIn.h"
#include "SKL/AMC/Backend/Codegen.h"
#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Artifact/Metadata.h"

#include <algorithm>
#include <cctype>
#include <stdint.h>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <set>
#include <vector>

#include <fmt/format.h>
#include <fmt/os.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

namespace skl::amc::lang::rust {
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

// True when `s` can be pasted as a Rust identifier without rewriting.
bool is_rust_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool is_rust_keyword(const std::string &s) {
    static const char * const keywords[] = {"as", "break", "const", "continue", "crate", "dyn", "else", "enum",
        "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref",
        "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where",
        "while", "async", "await", "box", "abstract", "become", "do", "final", "macro", "override", "priv", "try",
        "typeof", "unsized", "virtual", "yield"};
    for (const char *k : keywords)
        if (s == k) return true;
    return false;
}

// Map an arbitrary ABIX identifier to a legal Rust identifier. `::` becomes `_`
// and any other non-identifier character becomes `_`; a name that is empty or
// starts with a digit is prefixed.
std::string rust_ident(const std::string &name) {
    if (is_rust_identifier(name) && !is_rust_keyword(name)) return name;
    std::string out;
    out.reserve(name.size() + 1);
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            out += c;
        else
            out += '_';
    }
    if (out.empty()) out = "abix";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    if (is_rust_keyword(out)) out += '_';
    return out;
}

// Field identifier: keywords are escaped as raw identifiers so the ABI-visible
// spelling is preserved as closely as possible.
std::string rust_field_ident(const std::string &name) {
    const std::string ident = rust_ident(name);
    if (ident == "self" || ident == "Self" || ident == "super" || ident == "crate") return ident + "_";
    if (is_rust_keyword(ident)) return "r#" + ident;
    return ident;
}

std::string rust_const_ident(std::string name) {
    name = rust_ident(name);
    for (char &c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

// Convert an ABI name to a PascalCase Rust type identifier (for generated
// error types): non-alphanumeric characters start a new word.
std::string pascal_ident(const std::string &name) {
    std::string out;
    bool upper = true;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            upper = false;
        } else {
            upper = true;
        }
    }
    if (out.empty()) out = "Abix";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

std::string rust_string(const std::string &value) { return amc::backend::escape_string_literal(value); }

std::string hex_u64(uint64_t value) { return amc::backend::hex_u64(value); }

// Reduce a type spelling to its bare name by dropping C/C++ qualifier keywords
// and all separators, so that `const struct DemoVec2` and `DemoVec2` compare
// equal. Pointer/reference punctuation is dropped as well; callers that need
// to detect indirection do so on the original spelling.
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

// Identifies a type by TypeID so references can be resolved without a hash map.
class Projector {
public:
    explicit Projector(const amc::ABIModule &module, const amc::ir::LoweringResult *lowering)
        : m(module)
        , lowering(lowering) {
        names.reserve(m.types.size());
        std::unordered_map<std::string, uint32_t> used;
        for (const auto &type : m.types) {
            std::string candidate = rust_ident(type.name);
            const auto count = used[candidate]++;
            if (count != 0) candidate += fmt::format("_{}", count);
            names.push_back(std::move(candidate));
        }
        // Spelling -> type index for value types only. Pointers are excluded so
        // a pointee lookup cannot resolve back to a pointer.
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].kind != amc::TypeKind::POINTER)
                by_spelling.emplace(normalise_type_spelling(m.types[i].name), static_cast<int>(i));
    }

    int index_of(amc::Hash128 id) const {
        // Prefer the lowered contract: it already resolved the semantic entity
        // to an ABI index, so the backend does not scan or match by spelling.
        if (lowering != nullptr)
            for (const auto &type : lowering->types)
                if (type.abi == id && type.abi_index < m.types.size() && m.types[type.abi_index].id == id)
                    return static_cast<int>(type.abi_index);
        for (size_t i = 0; i < m.types.size(); ++i)
            if (m.types[i].id == id) return static_cast<int>(i);
        return -1;
    }

    const std::string &name_of(amc::Hash128 id) const {
        const int index = index_of(id);
        return index < 0 ? empty : names[static_cast<size_t>(index)];
    }

    // Rust type usable in a field/parameter position for `id`.
    std::string field_type(amc::Hash128 id) const {
        const int index = index_of(id);
        if (index < 0) return "core::ffi::c_void";
        const auto &type = m.types[static_cast<size_t>(index)];
        if (type.kind == amc::TypeKind::PRIMITIVE) return primitive_rust_type(type);
        if (type.kind == amc::TypeKind::POINTER) return pointer_rust_type(type);
        return names[static_cast<size_t>(index)];
    }

    // Rust type for a pointer, derived from its recorded spelling. ABIX keeps
    // the pointee only as part of the pointer's name (neither frontend links it
    // by TypeID yet), so the spelling is normalised and matched against the
    // value types in the module. Unresolvable pointees degrade to `c_void`.
    std::string pointer_rust_type(const amc::Type &type) const {
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
            if (n.rfind("&mut", 0) == 0) {
                pointee = n.substr(4);
            } else {
                is_const = true;
                pointee = n.substr(1);
            }
        } else {
            return "*mut core::ffi::c_void";
        }
        // A pointer-to-pointer (or any residual indirection) is not modelled.
        if (pointee.find('*') != std::string::npos || pointee.find('&') != std::string::npos)
            return is_const ? "*const core::ffi::c_void" : "*mut core::ffi::c_void";
        const std::string base = resolve_spelling(pointee);
        if (base.empty() || base == "()") return is_const ? "*const core::ffi::c_void" : "*mut core::ffi::c_void";
        return (is_const ? "*const " : "*mut ") + base;
    }

    // Rust type of the value a pointer points at (for out-parameters).
    std::string pointer_pointee_rust(const amc::Type &type) const {
        const std::string &n = type.name;
        std::string pointee;
        if (!n.empty() && n.back() == '*') {
            pointee = n.substr(0, n.size() - 1);
        } else if (n.rfind("*const", 0) == 0) {
            pointee = n.substr(6);
        } else if (n.rfind("*mut", 0) == 0) {
            pointee = n.substr(4);
        } else if (!n.empty() && n[0] == '&') {
            pointee = n.rfind("&mut", 0) == 0 ? n.substr(4) : n.substr(1);
        } else {
            return "core::ffi::c_void";
        }
        if (pointee.find('*') != std::string::npos || pointee.find('&') != std::string::npos)
            return "core::ffi::c_void";
        const std::string base = resolve_spelling(pointee);
        return base.empty() || base == "()" ? std::string("core::ffi::c_void") : base;
    }

    const amc::ABIModule &m;

    const amc::ir::LoweredExposedType *exposed_for(amc::Hash128 type_id) const {
        if (lowering == nullptr) return nullptr;
        for (const auto &record : lowering->exposed)
            if (record.abi == type_id && record.status == amc::ir::LoweringStatus::LOWERED) return &record;
        return nullptr;
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

    // A value can cross the generated API without the caller proving a raw
    // pointer contract: primitives, enums and records composed only of those.
    bool is_safe_value(amc::Hash128 id) const {
        std::set<std::pair<uint64_t, uint64_t>> seen;
        return is_safe_value_impl(id, seen);
    }

    bool is_safe_function(const amc::Function &function) const {
        const int return_index = index_of(function.return_type);
        if (return_index < 0) return false;
        const amc::Type &result = m.types[static_cast<size_t>(return_index)];
        const bool returns_void =
            result.kind == amc::TypeKind::PRIMITIVE
            && (result.primitive_abi & 0xffu) == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
        if (!returns_void && !is_safe_value(function.return_type)) return false;
        for (const auto &parameter : function.params)
            if (!is_safe_value(parameter.type_id)) return false;
        return true;
    }

private:
    bool is_safe_value_impl(amc::Hash128 id, std::set<std::pair<uint64_t, uint64_t>> &seen) const {
        const int index = index_of(id);
        if (index < 0) return false;
        const amc::Type &type = m.types[static_cast<size_t>(index)];
        if ((type.flags & amc::type_opaque) != 0) return false;
        if (!seen.emplace(id.lo, id.hi).second) return true;
        switch (type.kind) {
            case amc::TypeKind::PRIMITIVE:
                return (type.primitive_abi & 0xffu) != static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
            case amc::TypeKind::ENUM: return true;
            case amc::TypeKind::RECORD:
                for (uint32_t i = 0; i < type.field_count; ++i)
                    if (!is_safe_value_impl(m.fields[type.field_begin + i].type_id, seen)) return false;
                return true;
            case amc::TypeKind::ALIAS:
                if (type.field_count == 0) return false;
                return is_safe_value_impl(m.fields[type.field_begin].type_id, seen);
            default: return false;
        }
    }

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

// Underlying Rust integer for a `#[repr(C)]` enumeration of a given width.
std::string enum_underlying(uint32_t size) {
    switch (size) {
        case 1:  return "i8";
        case 2:  return "i16";
        case 8:  return "i64";
        case 16: return "i128";
        case 4:
        default: return "core::ffi::c_int";
    }
}

void emit_constants(Output &o, const Projector &p, const amc::Type &type) {
    const std::string &name = p.name_of(type.id);
    o.print("impl {} {{\n", name);
    o.print("    pub const ABIX_TYPE_ID_LO: u64 = {};\n", hex_u64(type.id.lo));
    o.print("    pub const ABIX_TYPE_ID_HI: u64 = {};\n", hex_u64(type.id.hi));
    o.print("    pub const ABIX_SIZE: usize = {};\n", type.size);
    o.print("    pub const ABIX_ALIGN: usize = {};\n", type.align);
    o.print("    pub const ABIX_FIELD_COUNT: u32 = {};\n", type.field_count);
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name.empty()) continue;
        o.print("    pub const ABIX_{}_OFFSET: usize = {};\n", rust_ident(field.name), field.offset);
    }
    o.print("}}\n");
}

void emit_record(Output &o, const Projector &p, const amc::Type &type) {
    const std::string &name = p.name_of(type.id);
    // A record with no fields is opaque (a forward declaration): emit an
    // explicitly sized, aligned byte blob instead of an empty struct.
    if (type.field_count == 0) {
        o.print("#[repr(C, align({}))]\n", type.align == 0 ? 1 : type.align);
        o.print("pub struct {}([u8; {}]);\n", name, type.size == 0 ? 1 : type.size);
        emit_constants(o, p, type);
        return;
    }
    // Natural alignment implied by the fields as projected. A larger recorded
    // alignment comes from an explicit `#[repr(C, align(N))]` and must be
    // re-emitted, otherwise the projected type would be under-aligned.
    uint32_t natural = 1;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        const int index = p.index_of(field.type_id);
        if (index >= 0) natural = std::max(natural, p.m.types[static_cast<size_t>(index)].align);
    }
    if (type.align > natural)
        o.print("#[repr(C, align({}))]\n", type.align);
    else
        o.print("#[repr(C)]\n");
    o.print("pub struct {} {{\n", name);
    uint32_t base_index = 0;
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        const bool is_base = (field.flags & amc::field_base) != 0;
        if (is_base) {
            o.print("    pub __base{}: {},\n", base_index++, p.field_type(field.type_id));
            continue;
        }
        if (field.name.empty()) continue;
        o.print("    pub {}: {},\n", rust_field_ident(field.name), p.field_type(field.type_id));
    }
    o.print("}}\n");
    emit_constants(o, p, type);
}

void emit_enumeration(Output &o, const Projector &p, const amc::Type &type) {
    // ABIX does not retain enumerator values, so the enumeration projects to its
    // underlying integer type; layout compatibility is still enforced below.
    o.print("pub type {} = {};\n", p.name_of(type.id), enum_underlying(type.size));
}

void emit_alias(Output &o, const Projector &p, const amc::Type &type) {
    std::string underlying = "core::ffi::c_void";
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const auto &field = p.m.fields[type.field_begin + i];
        if (field.name == "underlying") {
            underlying = p.field_type(field.type_id);
            break;
        }
    }
    o.print("pub type {} = {};\n", p.name_of(type.id), underlying);
}

// A type a consumer should not reconstruct: STL types, template spellings and
// references require an explicit contract (StringView, Opaque, ...) rather than
// a guessed Rust struct. ABIX-STD boundary types (abix_*) are projected.
static bool is_projectable_name(const std::string &name) {
    if (name.rfind("abix_", 0) == 0) return true;
    if (name.find("std::") != std::string::npos) return false;
    if (name.find('<') != std::string::npos) return false;
    if (name.find('&') != std::string::npos) return false;
    return true;
}

void emit_opaque(Output &o, const Projector &p, const amc::Type &type) {
    // Arrays (the element type is not part of the ABI record) and other
    // unmodelled aggregates project to an explicitly sized, explicitly aligned
    // byte blob so size and alignment stay exact.
    o.print("#[repr(C, align({}))]\n", type.align == 0 ? 1 : type.align);
    o.print("pub struct {}([u8; {}]);\n", p.name_of(type.id), type.size);
    emit_constants(o, p, type);
}

void emit_type(Output &o, const Projector &p, const amc::Type &type) {
    // An exposed type is an opaque handle for consumers: emit a sized blob so
    // the fields are not accessible, even though the producer keeps the layout.
    if ((type.flags & amc::type_opaque) != 0) {
        emit_opaque(o, p, type);
        return;
    }
    switch (type.kind) {
        case amc::TypeKind::RECORD: emit_record(o, p, type); break;
        case amc::TypeKind::ENUM:   emit_enumeration(o, p, type); break;
        case amc::TypeKind::ALIAS:  emit_alias(o, p, type); break;
        case amc::TypeKind::POINTER:
            o.print("pub type {} = {};\n", p.name_of(type.id), p.pointer_rust_type(type));
            break;
        case amc::TypeKind::ARRAY:     emit_opaque(o, p, type); break;
        case amc::TypeKind::NAMESPACE: break;
        case amc::TypeKind::PRIMITIVE: break;
        case amc::TypeKind::FUNC:      break;
    }
}

void emit_functions(Output &o, const Projector &p) {
    std::vector<const amc::Function *> free_functions;
    for (const auto &function : p.m.functions)
        if (function.owner_type.lo == 0 && function.owner_type.hi == 0) free_functions.push_back(&function);
    if (free_functions.empty()) return;
    // The library that owns these symbols. It is the package name by
    // convention; a consumer opts into link-time resolution by enabling the
    // `abix_link` cfg, otherwise the declarations stay usable through a
    // runtime loader that resolves them itself.
    const std::string &library = p.m.package_name;
    const bool linkable = !library.empty() && library.find_first_of(" \t\"\\") == std::string::npos;

    struct Signature {
        std::string params;
        std::string args;
        bool returns_void = false;
        bool safe = false;
    };
    std::vector<Signature> signatures;
    signatures.reserve(free_functions.size());

    // Raw entry points stay in one module. Consumers should use the wrappers
    // below; the module is public so an advanced consumer can reach the exact
    // C symbol, but doing so opts back into the raw unsafe contract.
    o.print("\n// Raw C entry points. Use the generated wrappers below for the\n");
    o.print("// supported safe surface, or `amc_ffi!` when a raw call is required.\n");
    if (linkable) o.print("// Enable `abix_link` to resolve them against `{}` at link time.\n", library);
    o.print("#[doc(hidden)]\npub mod raw {{\n    use super::*;\n");
    for (const auto *function : free_functions) {
        Signature signature;
        for (size_t i = 0; i < function->params.size(); ++i) {
            const auto &parameter = function->params[i];
            const std::string parameter_name =
                parameter.name.empty() ? fmt::format("arg{}", i) : rust_field_ident(parameter.name);
            if (i != 0) {
                signature.params += ", ";
                signature.args += ", ";
            }
            signature.params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
            signature.args += parameter_name;
        }
        const int return_index = p.index_of(function->return_type);
        signature.returns_void = return_index >= 0
                              && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::PRIMITIVE
                              && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                     == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
        signature.safe = p.is_safe_function(*function);
        if (linkable) o.print("    #[cfg_attr(abix_link, link(name = {}))]\n", rust_string(library));
        if (signature.returns_void)
            o.print(
                "    unsafe extern \"C\" {{ pub fn {}({}) -> (); }}\n", rust_ident(function->name), signature.params);
        else
            o.print("    unsafe extern \"C\" {{ pub fn {}({}) -> {}; }}\n", rust_ident(function->name),
                signature.params, p.field_type(function->return_type));
        signatures.push_back(std::move(signature));
    }
    o.print("}}\n\n");

    // Every generated wrapper funnels its raw call through this single macro,
    // so the `unsafe` block lives in one place instead of being repeated (and
    // accidentally widened) by consumers.
    o.print("/// Wrap one raw ABI call in the single generated `unsafe` block.\n");
    o.print("#[allow(unused_macros)]\nmacro_rules! amc_ffi {{\n");
    o.print("    ($function:ident($($arg:expr),* $(,)?)) => {{\n");
    o.print("        unsafe {{ raw::$function($($arg),*) }}\n");
    o.print("    }};\n}}\n\n");

    for (size_t index = 0; index < free_functions.size(); ++index) {
        const auto *function = free_functions[index];
        const Signature &signature = signatures[index];
        if (signature.safe) {
            o.print("/// Safe wrapper for the `{}` ABI entry point.\n", function->name);
            o.print("#[inline]\npub fn {}({})", rust_ident(function->name), signature.params);
        } else {
            o.print("/// Wrapper for the `{}` ABI entry point.\n", function->name);
            o.print("///\n/// The caller must uphold the raw pointer / owner contract.\n");
            o.print("#[inline]\npub unsafe fn {}({})", rust_ident(function->name), signature.params);
        }
        if (!signature.returns_void) o.print(" -> {}", p.field_type(function->return_type));
        o.print(" {{\n");
        if (signature.returns_void)
            o.print("    amc_ffi!({}({}));\n", rust_ident(function->name), signature.args);
        else
            o.print("    amc_ffi!({}({}))\n", rust_ident(function->name), signature.args);
        o.print("}}\n");
    }
}

void emit_owned(Output &o, const Projector &p) {
    if (p.m.owned.empty()) return;
    o.print(
        "\n// Ownership wrappers: RAII handles released through the declared\n"
        "// destroy entry point, so a consumer never frees foreign memory itself.\n");
    for (const auto &record : p.m.owned) {
        const int index = p.index_of(record.type_id);
        if (index < 0) continue;
        const amc::Type &type = p.m.types[static_cast<size_t>(index)];
        if (type.kind != amc::TypeKind::RECORD) continue;
        const amc::Function *destroy = p.function_by_signature(record.destroy);
        if (!destroy) continue;
        const amc::Function *create = p.function_by_signature(record.create);
        const std::string type_name = p.name_of(record.type_id);
        const std::string wrapper = "Owned" + type_name;
        o.print("pub struct {} {{\n    ptr: *mut {},\n}}\n", wrapper, type_name);
        o.print("impl {} {{\n", wrapper);
        o.print("    /// Wrap a raw handle returned by the library; `None` when null.\n");
        o.print("    pub unsafe fn from_raw(ptr: *mut {}) -> Option<Self> {{\n", type_name);
        o.print("        if ptr.is_null() {{ None }} else {{ Some({} {{ ptr }}) }}\n", wrapper);
        o.print("    }}\n");
        if (create) {
            std::string params, args;
            for (size_t i = 0; i < create->params.size(); ++i) {
                const auto &parameter = create->params[i];
                const std::string parameter_name =
                    parameter.name.empty() ? fmt::format("arg{}", i) : rust_field_ident(parameter.name);
                if (i != 0) {
                    params += ", ";
                    args += ", ";
                }
                params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
                args += parameter_name;
            }
            o.print("    /// Call `{}` and take ownership of the result.\n", create->name);
            o.print("    pub fn create({}) -> Option<Self> {{\n", params);
            o.print("        unsafe {{ Self::from_raw(amc_ffi!({}({}))) }}\n", rust_ident(create->name), args);
            o.print("    }}\n");
        }
        o.print("    pub fn as_ptr(&self) -> *mut {} {{ self.ptr }}\n", type_name);
        o.print("    pub fn as_ref(&self) -> &{} {{ unsafe {{ &*self.ptr }} }}\n", type_name);
        o.print(
            "    pub fn into_raw(self) -> *mut {} {{ let ptr = self.ptr; core::mem::forget(self); ptr }}\n", type_name);
        // Method wrappers come from the explicit exposed-handle contract. The
        // thunk names are never parsed here: each method already points at an
        // ABI signature.
        if (const auto *exposed = p.exposed_for(record.type_id)) {
            for (const auto &method : exposed->methods) {
                const amc::Function *function = p.function_by_signature(method.abi);
                if (function == nullptr || function->params.empty()) continue;
                const std::string method_name = rust_field_ident(method.name);
                if (method_name == "create"
                    || method_name == "destroy"
                    || method_name == "from_raw"
                    || method_name == "as_ptr"
                    || method_name == "as_ref"
                    || method_name == "into_raw")
                    continue;
                std::string params, args;
                for (size_t i = 1; i < function->params.size(); ++i) {
                    const auto &parameter = function->params[i];
                    const std::string parameter_name =
                        parameter.name.empty() ? fmt::format("arg{}", i - 1) : rust_field_ident(parameter.name);
                    params += fmt::format(", {}: {}", parameter_name, p.field_type(parameter.type_id));
                    args += fmt::format(", {}", parameter_name);
                }
                const int return_index = p.index_of(function->return_type);
                const bool returns_void = return_index >= 0
                                       && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::PRIMITIVE
                                       && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                              == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
                if (returns_void)
                    o.print("    pub fn {}(&mut self{}) {{ amc_ffi!({}(self.ptr{})); }}\n", method_name, params,
                        rust_ident(function->name), args);
                else
                    o.print("    pub fn {}(&mut self{}) -> {} {{ amc_ffi!({}(self.ptr{})) }}\n", method_name, params,
                        p.field_type(function->return_type), rust_ident(function->name), args);
            }
        }
        o.print("}}\n");
        o.print("impl Drop for {} {{\n    fn drop(&mut self) {{\n", wrapper);
        o.print("        if !self.ptr.is_null() {{ amc_ffi!({}(self.ptr)); }}\n", rust_ident(destroy->name));
        o.print("    }}\n}}\n");
    }
}

const amc::Field *find_visible_field(const amc::ABIModule &module, const amc::Type &type, const char *name) {
    for (uint32_t i = 0; i < type.field_count; ++i) {
        const amc::Field &field = module.fields[type.field_begin + i];
        if ((field.flags & amc::field_base) == 0 && (field.flags & amc::field_bitfield) == 0 && field.name == name)
            return &field;
    }
    return nullptr;
}

void emit_owner_views(Output &o, const Projector &p) {
    if (p.m.lowerings.empty()) return;
    std::set<std::string> emitted;
    bool header = false;
    for (const auto &lowering : p.m.lowerings) {
        const auto kind = static_cast<amc::LoweringKind>(lowering.kind);
        if (kind != amc::LoweringKind::OWNER
            && kind != amc::LoweringKind::OWNED_STRING
            && kind != amc::LoweringKind::OWNED_SPAN)
            continue;
        const int index = p.index_of(lowering.type_id);
        if (index < 0) continue;
        const amc::Type &type = p.m.types[static_cast<size_t>(index)];
        const std::string raw_name = p.name_of(lowering.type_id);
        if (raw_name.empty() || !emitted.emplace(raw_name).second) continue;
        if (!header) {
            o.print(
                "\n// ABIX owner handles and owned views: value wrappers that\n"
                "// release through the owner destroy entry exactly once.\n");
            header = true;
        }
        if (kind == amc::LoweringKind::OWNER) {
            const amc::Field *object = find_visible_field(p.m, type, "object");
            const amc::Field *mode = find_visible_field(p.m, type, "mode");
            const amc::Field *destroy = find_visible_field(p.m, type, "destroy");
            if (!object || !mode || !destroy) continue;
            std::string suffix = raw_name;
            const std::string prefix = "abix_owner_";
            if (suffix.rfind(prefix, 0) == 0) suffix = suffix.substr(prefix.size());
            const std::string wrapper = "OwnerOf" + pascal_ident(suffix);
            o.print("pub struct {} {{\n    raw: {},\n}}\n", wrapper, raw_name);
            o.print("impl {} {{\n", wrapper);
            o.print("    pub unsafe fn from_raw(raw: {}) -> Option<Self> {{\n", raw_name);
            o.print("        if raw.object.is_null() {{ None }} else {{ Some(Self {{ raw }}) }}\n    }}\n");
            o.print("    pub fn object(&self) -> {} {{ self.raw.object }}\n", p.field_type(object->type_id));
            o.print("    pub fn into_raw(self) -> {} {{\n", raw_name);
            o.print("        let raw = unsafe {{ core::ptr::read(&self.raw) }};\n");
            o.print("        core::mem::forget(self);\n        raw\n    }}\n}}\n");
            o.print("impl Drop for {} {{\n", wrapper);
            o.print("    fn drop(&mut self) {{\n");
            o.print("        if !self.raw.destroy.is_null() && (self.raw.mode == 1 || self.raw.mode == 2) {{\n");
            o.print("            let destroy: unsafe extern \"C\" fn(*mut core::ffi::c_void) =\n");
            o.print("                unsafe {{ core::mem::transmute(self.raw.destroy) }};\n");
            o.print("            if !self.raw.object.is_null() {{ unsafe {{ destroy(self.raw.object); }} }}\n");
            o.print("        }}\n        self.raw.object = core::ptr::null_mut();\n    }}\n}}\n");
        } else {
            const amc::Field *view = find_visible_field(p.m, type, "view");
            const amc::Field *owner = find_visible_field(p.m, type, "owner");
            if (!view || !owner) continue;
            std::string suffix = raw_name;
            const std::string prefix = kind == amc::LoweringKind::OWNED_STRING ? "abix_owned_str_" : "abix_owned_span_";
            if (suffix.rfind(prefix, 0) == 0) suffix = suffix.substr(prefix.size());
            const std::string wrapper =
                (kind == amc::LoweringKind::OWNED_STRING ? "OwnedString" : "OwnedSpan") + pascal_ident(suffix);
            const std::string view_type = p.field_type(view->type_id);
            o.print("pub struct {} {{\n    raw: {},\n}}\n", wrapper, raw_name);
            o.print("impl {} {{\n", wrapper);
            o.print("    pub unsafe fn from_raw(raw: {}) -> Option<Self> {{\n", raw_name);
            o.print("        if raw.owner.object.is_null() {{ None }} else {{ Some(Self {{ raw }}) }}\n    }}\n");
            o.print("    pub fn view(&self) -> &{} {{ &self.raw.view }}\n", view_type);
            o.print("    pub fn into_raw(self) -> {} {{\n", raw_name);
            o.print("        let raw = unsafe {{ core::ptr::read(&self.raw) }};\n");
            o.print("        core::mem::forget(self);\n        raw\n    }}\n}}\n");
            o.print("impl Drop for {} {{\n", wrapper);
            o.print("    fn drop(&mut self) {{\n");
            o.print("        if !self.raw.owner.destroy.is_null()\n");
            o.print("            && (self.raw.owner.mode == 1 || self.raw.owner.mode == 2)\n        {{\n");
            o.print("            let destroy: unsafe extern \"C\" fn(*mut core::ffi::c_void) =\n");
            o.print("                unsafe {{ core::mem::transmute(self.raw.owner.destroy) }};\n");
            o.print(
                "            if !self.raw.owner.object.is_null() {{ unsafe {{ destroy(self.raw.owner.object); }} }}\n");
            o.print("        }}\n        self.raw.owner.object = core::ptr::null_mut();\n    }}\n}}\n");
        }
    }
}

void emit_capability_contracts(Output &o, const Projector &p) {
    if (p.m.interfaces.empty()) return;
    o.print("\n// Interface capability contracts: identity, version, slots and signatures.\n");
    o.print("pub mod capabilities {{\n");
    o.print("    #[derive(Clone, Copy)]\n");
    o.print(
        "    pub struct Method {{\n        pub name: &'static str,\n        pub slot: u32,\n"
        "        pub signature_lo: u64,\n        pub signature_hi: u64,\n    }}\n");
    o.print("    #[derive(Clone, Copy)]\n");
    o.print(
        "    pub struct Contract {{\n        pub name: &'static str,\n        pub id_lo: u64,\n"
        "        pub id_hi: u64,\n        pub version: u32,\n        pub methods: &'static [Method],\n    }}\n");
    std::vector<std::string> names;
    for (const auto &record : p.m.interfaces) {
        std::vector<const amc::InterfaceMethod *> by_slot;
        for (uint32_t i = 0; i < record.method_count; ++i) {
            const auto &method = p.m.interface_methods[record.method_begin + i];
            if (method.slot >= by_slot.size()) by_slot.resize(method.slot + 1, nullptr);
            by_slot[method.slot] = &method;
        }
        const std::string constant = rust_const_ident(record.name) + "_CONTRACT";
        names.push_back(constant);
        o.print("    pub const {}: Contract = Contract {{\n", constant);
        o.print("        name: {},\n", rust_string(record.name));
        o.print("        id_lo: 0x{:x},\n        id_hi: 0x{:x},\n", record.id.lo, record.id.hi);
        o.print("        version: {},\n", record.version);
        o.print("        methods: &[\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            o.print("            Method {{ name: {}, slot: {}, signature_lo: 0x{:x}, signature_hi: 0x{:x} }},\n",
                rust_string(method->name), method->slot, method->signature.lo, method->signature.hi);
        }
        o.print("        ],\n    }};\n");
    }
    o.print("    pub const ALL: &[Contract] = &[\n");
    for (const auto &name : names)
        o.print("        {},\n", name);
    o.print("    ];\n}}\n");
}

void emit_interface_proxies(Output &o, const Projector &p) {
    if (p.m.interfaces.empty()) return;
    o.print("\n// Interface proxies: bind-time version/slot/signature validation.\n");
    o.print("#[repr(C)]\n#[derive(Clone, Copy)]\n");
    o.print(
        "pub struct InterfaceMethodEntry {{\n    pub signature_lo: u64,\n    pub signature_hi: u64,\n"
        "    pub entry: usize,\n    pub slot: u32,\n    pub flags: u32,\n}}\n");
    o.print("#[derive(Clone, Copy, PartialEq, Eq, Debug)]\n");
    o.print(
        "pub enum BindError {{\n    InterfaceNotFound,\n    VersionMismatch,\n    MethodMissing,\n"
        "    SignatureMismatch,\n}}\n");
    o.print("/// Opaque module/loader lease held by an interface proxy.\n");
    o.print("pub struct InterfaceLease {{\n    raw: *mut core::ffi::c_void,\n");
    o.print("    release: unsafe extern \"C\" fn(*mut core::ffi::c_void),\n}}\n");
    o.print("impl InterfaceLease {{\n");
    o.print("    /// The caller must pass a token whose release function keeps the\n");
    o.print("    /// module alive until the proxy is dropped.\n");
    o.print("    pub unsafe fn from_raw(raw: *mut core::ffi::c_void,\n");
    o.print("        release: unsafe extern \"C\" fn(*mut core::ffi::c_void)) -> Self {{\n");
    o.print("        Self {{ raw, release }}\n    }}\n}}\n");
    o.print("impl Drop for InterfaceLease {{\n    fn drop(&mut self) {{\n");
    o.print("        unsafe {{ (self.release)(self.raw); }}\n    }}\n}}\n");
    for (const auto &record : p.m.interfaces) {
        std::vector<const amc::InterfaceMethod *> by_slot;
        for (uint32_t i = 0; i < record.method_count; ++i) {
            const auto &method = p.m.interface_methods[record.method_begin + i];
            if (method.slot >= by_slot.size()) by_slot.resize(method.slot + 1, nullptr);
            by_slot[method.slot] = &method;
        }
        const std::string type = pascal_ident(record.name) + "Proxy";
        o.print("\npub struct {} {{\n    object: *mut core::ffi::c_void,\n", type);
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            std::string params = "*mut core::ffi::c_void";
            for (size_t i = method->parameters.empty() ? 0 : 1; i < method->parameters.size(); ++i)
                params += ", " + p.field_type(method->parameters[i].type_id);
            const int return_index = p.index_of(method->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::PRIMITIVE
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
            o.print("    {}: unsafe extern \"C\" fn({}){},\n", rust_field_ident(method->name), params,
                returns_void ? "" : " -> " + p.field_type(method->return_type));
        }
        o.print("    lease: Option<InterfaceLease>,\n}}\n");

        o.print("impl {} {{\n", type);
        o.print("    pub const ID_LO: u64 = 0x{:x};\n    pub const ID_HI: u64 = 0x{:x};\n", record.id.lo, record.id.hi);
        o.print("    pub const VERSION: u32 = {};\n", record.version);
        o.print("    const SLOT_CAPACITY: usize = {};\n", by_slot.size());
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            o.print("    pub const {}_SLOT: u32 = {};\n", rust_const_ident(method->name), method->slot);
            o.print("    pub const {}_SIGNATURE: (u64, u64) = (0x{:x}, 0x{:x});\n", rust_const_ident(method->name),
                method->signature.lo, method->signature.hi);
        }
        o.print(
            "    pub unsafe fn bind(interface_id: (u64, u64), version: u32, object: *mut core::ffi::c_void,\n"
            "        methods: &[InterfaceMethodEntry], lease: Option<InterfaceLease>) -> Result<Self, BindError> "
            "{{\n");
        o.print(
            "        if interface_id != (Self::ID_LO, Self::ID_HI) {{ return Err(BindError::InterfaceNotFound); }}\n");
        o.print("        if version != Self::VERSION {{ return Err(BindError::VersionMismatch); }}\n");
        o.print("        let mut entries = [0usize; Self::SLOT_CAPACITY];\n");
        o.print("        let mut signatures = [(0u64, 0u64); Self::SLOT_CAPACITY];\n");
        o.print(
            "        for method in methods {{\n            if method.entry == 0 || method.slot as usize >= "
            "Self::SLOT_CAPACITY {{ continue; }}\n");
        o.print("            entries[method.slot as usize] = method.entry;\n");
        o.print(
            "            signatures[method.slot as usize] = (method.signature_lo, method.signature_hi);\n        }}\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            const std::string name = rust_const_ident(method->name);
            o.print(
                "        if entries[Self::{}_SLOT as usize] == 0 {{ return Err(BindError::MethodMissing); }}\n", name);
            o.print(
                "        if signatures[Self::{}_SLOT as usize] != Self::{}_SIGNATURE {{ return "
                "Err(BindError::SignatureMismatch); }}\n",
                name, name);
        }
        o.print("        Ok(Self {{\n            object,\n            lease,\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            std::string params = "*mut core::ffi::c_void";
            for (size_t i = method->parameters.empty() ? 0 : 1; i < method->parameters.size(); ++i)
                params += ", " + p.field_type(method->parameters[i].type_id);
            const int return_index = p.index_of(method->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::PRIMITIVE
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
            o.print(
                "            {}: core::mem::transmute::<usize, unsafe extern \"C\" fn({}){}>(entries[Self::{}_SLOT as "
                "usize]),\n",
                rust_field_ident(method->name), params, returns_void ? "" : " -> " + p.field_type(method->return_type),
                rust_const_ident(method->name));
        }
        o.print("        }})\n    }}\n");
        o.print("    pub fn has_lease(&self) -> bool {{ self.lease.is_some() }}\n");
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            std::string params, args;
            for (size_t i = method->parameters.empty() ? 0 : 1; i < method->parameters.size(); ++i) {
                if (!params.empty()) {
                    params += ", ";
                    args += ", ";
                }
                const std::string name = "arg" + std::to_string(i - (method->parameters.empty() ? 0 : 1));
                params += name + ": " + p.field_type(method->parameters[i].type_id);
                args += name;
            }
            const int return_index = p.index_of(method->return_type);
            const bool returns_void = return_index >= 0
                                   && p.m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::PRIMITIVE
                                   && (p.m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveABIKind::AMC_VOID);
            if (returns_void)
                o.print("    pub fn {}(&self{}) {{ unsafe {{ (self.{})(self.object{}) }} }}\n",
                    rust_field_ident(method->name), params.empty() ? "" : ", " + params, rust_field_ident(method->name),
                    args.empty() ? "" : ", " + args);
            else
                o.print("    pub fn {}(&self{}) -> {} {{ unsafe {{ (self.{})(self.object{}) }} }}\n",
                    rust_field_ident(method->name), params.empty() ? "" : ", " + params,
                    p.field_type(method->return_type), rust_field_ident(method->name), args.empty() ? "" : ", " + args);
        }
        o.print("}}\n");
    }
}

const amc::ErrorSet *find_error_set(const amc::ABIModule &module, amc::Hash128 id) {
    if (id.lo == 0 && id.hi == 0) return nullptr;
    for (const auto &set : module.error_sets)
        if (set.id == id) return &set;
    return nullptr;
}

const amc::ErrorCode *find_error_code(const amc::ABIModule &module, const amc::ErrorSet &set, int32_t value) {
    for (uint32_t i = 0; i < set.code_count; ++i) {
        const auto &code = module.error_codes[set.code_begin + i];
        if (code.value == value) return &code;
    }
    return nullptr;
}

// Named error sets become a stable Rust value type. Unknown codes are preserved
// as their raw numeric value; only known codes get a name and condition.
void emit_error_sets(Output &o, const Projector &p) {
    if (p.m.error_sets.empty()) return;
    o.print("\n// Named error sets: stable values, conditions and names.\n");
    for (const auto &set : p.m.error_sets) {
        const std::string type = pascal_ident(set.name);
        o.print("#[derive(Clone, Copy, PartialEq, Eq, Debug)]\n");
        o.print("pub struct {}(pub i32);\n", type);
        o.print("impl {} {{\n", type);
        o.print("    pub const DOMAIN: u64 = 0x{:x};\n", static_cast<unsigned long long>(set.domain));
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = p.m.error_codes[set.code_begin + i];
            o.print("    pub const {}: Self = Self({});\n", rust_const_ident(code.name), code.value);
        }
        o.print("    pub const fn name(self) -> &'static str {{\n        match self.0 {{\n");
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = p.m.error_codes[set.code_begin + i];
            o.print("            {} => \"{}\",\n", code.value, code.name);
        }
        o.print("            _ => \"unknown\",\n        }}\n    }}\n");
        o.print("    pub const fn condition(self) -> i32 {{\n        match self.0 {{\n");
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = p.m.error_codes[set.code_begin + i];
            o.print("            {} => {},\n", code.value, code.condition);
        }
        o.print("            _ => -1,\n        }}\n    }}\n");
        o.print("    pub const fn condition_name(self) -> &'static str {{\n        match self.condition() {{\n");
        std::set<int32_t> conditions;
        for (uint32_t i = 0; i < set.code_count; ++i)
            conditions.insert(p.m.error_codes[set.code_begin + i].condition);
        for (const int32_t condition : conditions)
            o.print("            {} => \"{}\",\n", condition,
                amc::generic_condition_name(static_cast<amc::GenericCondition>(condition)));
        o.print("            _ => \"unknown\",\n        }}\n    }}\n}}\n");
    }
}

// Error-boundary wrappers: turn a C status code into a typed `Result`, so the
// language-native error mechanism (C++ exception / Rust panic) never has to
// cross the boundary. Only the declared contract is used -- nothing is inferred.
void emit_errors(Output &o, const Projector &p) {
    if (p.m.errors.empty()) return;
    o.print("\n// Error-boundary wrappers: a C status code becomes a typed Result.\n");
    for (const auto &record : p.m.errors) {
        if (record.function_index >= p.m.functions.size()) continue;
        const amc::Function &function = p.m.functions[record.function_index];
        const std::string function_rust = rust_ident(function.name);
        const amc::ErrorSet *set = find_error_set(p.m, record.error_set);
        const std::string error_type = set != nullptr ? pascal_ident(set->name) : pascal_ident(function.name) + "Error";
        if (record.status_source == amc::StatusSource::RETURN_FIELD) {
            // A Result return: the status and value are fields of the returned
            // record, so no out-parameter is involved.
            const int result_index = p.index_of(function.return_type);
            if (result_index < 0) continue;
            const amc::Type &result = p.m.types[static_cast<size_t>(result_index)];
            if (result.kind != amc::TypeKind::RECORD) continue;
            if (record.status_parameter >= result.field_count || record.value_parameter >= result.field_count) continue;
            const amc::Field &status_field = p.m.fields[result.field_begin + record.status_parameter];
            const amc::Field &value_field = p.m.fields[result.field_begin + record.value_parameter];
            const std::string status_rust = p.field_type(status_field.type_id);
            const std::string value_rust = p.field_type(value_field.type_id);
            const std::string status_access = rust_field_ident(status_field.name);
            const std::string value_access = rust_field_ident(value_field.name);

            std::string params, call_args;
            for (size_t i = 0; i < function.params.size(); ++i) {
                const auto &parameter = function.params[i];
                const std::string parameter_name =
                    parameter.name.empty() ? fmt::format("arg{}", i) : rust_field_ident(parameter.name);
                if (i != 0) {
                    params += ", ";
                    call_args += ", ";
                }
                params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
                call_args += parameter_name;
            }
            if (set == nullptr) {
                o.print("#[derive(Clone, Copy, PartialEq, Eq, Debug)]\n");
                o.print("pub struct {}(pub {});\n", error_type, status_rust);
                o.print(
                    "impl {} {{\n    pub const SUCCESS: {} = {};\n}}\n", error_type, status_rust, record.success_value);
            }
            o.print("pub fn {}({}) -> Result<{}, {}> {{\n", function_rust + "_result", params, value_rust, error_type);
            o.print("    let result = amc_ffi!({}({}));\n", function_rust, call_args);
            if (set != nullptr)
                o.print("    if result.{} == {} {{\n", status_access, record.success_value);
            else
                o.print("    if result.{} == {}::SUCCESS {{\n", status_access, error_type);
            o.print("        Ok(result.{})\n", value_access);
            o.print("    }} else {{\n        Err({}(result.{}))\n    }}\n}}\n", error_type, status_access);
            continue;
        }
        if (record.status_source != amc::StatusSource::RETURN_VALUE) continue;   // return-value status only
        if (record.value_parameter == UINT32_MAX) continue;
        if (record.value_parameter >= function.params.size()) continue;
        const int value_index = p.index_of(function.params[record.value_parameter].type_id);
        if (value_index < 0) continue;
        const amc::Type &value_type = p.m.types[static_cast<size_t>(value_index)];
        if (value_type.kind != amc::TypeKind::POINTER) continue;
        const std::string value_rust = p.pointer_pointee_rust(value_type);
        const std::string status_rust = p.field_type(function.return_type);

        std::string params, call_args;
        for (size_t i = 0; i < function.params.size(); ++i) {
            const bool is_value = (i == record.value_parameter);
            const auto &parameter = function.params[i];
            const std::string parameter_name =
                parameter.name.empty() ? fmt::format("arg{}", i) : rust_field_ident(parameter.name);
            if (!call_args.empty()) call_args += ", ";
            if (is_value) {
                call_args += "&mut value";
                continue;
            }
            if (!params.empty()) params += ", ";
            params += fmt::format("{}: {}", parameter_name, p.field_type(parameter.type_id));
            call_args += parameter_name;
        }

        if (set == nullptr) {
            o.print("#[derive(Clone, Copy, PartialEq, Eq, Debug)]\n");
            o.print("pub struct {}(pub {});\n", error_type, status_rust);
            o.print("impl {} {{\n    pub const SUCCESS: {} = {};\n}}\n", error_type, status_rust, record.success_value);
        }
        o.print("pub fn {}({}) -> Result<{}, {}> {{\n", function_rust + "_result", params, value_rust, error_type);
        o.print("    let mut value: {} = core::default::Default::default();\n", value_rust);
        o.print("    let status = amc_ffi!({}({}));\n", function_rust, call_args);
        if (set != nullptr)
            o.print(
                "    if status == {} {{ Ok(value) }} else {{ Err({}(status)) }}\n", record.success_value, error_type);
        else
            o.print(
                "    if status == {}::SUCCESS {{ Ok(value) }} else {{ Err({}(status)) }}\n", error_type, error_type);
        o.print("}}\n");
    }
}

// ABIX-STD lowering adapters: when a concrete instantiated type is compatible
// with a contract but not already the boundary entity, emit a thin conversion.
// A successful instantiation is not by itself a contract match.
void emit_lowerings(Output &o, const Projector &p) {
    for (const auto &record : p.m.lowerings) {
        if (record.status != static_cast<uint32_t>(amc::LoweringStatus::NEEDS_ADAPTER)) continue;
        const int concrete = p.index_of(record.type_id);
        const int canonical = p.index_of(record.contract_type);
        if (concrete < 0 || canonical < 0) continue;
        const amc::Type &source = p.m.types[static_cast<size_t>(concrete)];
        const amc::Type &target = p.m.types[static_cast<size_t>(canonical)];
        if (target.field_count < 2
            || record.pointer_field >= source.field_count
            || record.length_field >= source.field_count)
            continue;
        const std::string source_name = p.name_of(record.type_id);
        const std::string target_name = p.name_of(record.contract_type);
        const auto &target_data = p.m.fields[target.field_begin];
        const auto &target_len = p.m.fields[target.field_begin + 1];
        const auto &source_data = p.m.fields[source.field_begin + record.pointer_field];
        const auto &source_len = p.m.fields[source.field_begin + record.length_field];
        o.print("\n// ABIX-STD lowering adapter: {} -> {} (sequence-view).\n", source_name, target_name);
        o.print("pub fn {}_to_{}(value: {}) -> {} {{\n", source_name, target_name, source_name, target_name);
        o.print("    {} {{ {}: value.{}, {}: value.{} as _ }}\n", target_name, rust_field_ident(target_data.name),
            rust_field_ident(source_data.name), rust_field_ident(target_len.name), rust_field_ident(source_len.name));
        o.print("}}\n");
    }
}

// Container adapters: lower a container to a sequence view through its accessor
// functions, without exposing the container's internal layout.
void emit_view_adapters(Output &o, const Projector &p) {
    for (const auto &adapter : p.m.view_adapters) {
        const int source = p.index_of(adapter.source_type);
        const int target = p.index_of(adapter.target_type);
        const amc::Function *data = p.function_by_signature(adapter.data_accessor);
        const amc::Function *size = p.function_by_signature(adapter.size_accessor);
        if (source < 0 || target < 0 || !data || !size) continue;
        const amc::Type &target_type = p.m.types[static_cast<size_t>(target)];
        if (target_type.field_count < 2) continue;
        const std::string source_name = p.name_of(adapter.source_type);
        const std::string target_name = p.name_of(adapter.target_type);
        const auto &target_data = p.m.fields[target_type.field_begin];
        const auto &target_len = p.m.fields[target_type.field_begin + 1];
        o.print("\n// ABIX-STD container adapter: {} -> {} via accessors.\n", source_name, target_name);
        o.print("pub unsafe fn {}_view(value: *const {}) -> {} {{\n", source_name, source_name, target_name);
        o.print("    {} {{ {}: {}(value), {}: {}(value) as _ }}\n", target_name, rust_field_ident(target_data.name),
            rust_ident(data->name), rust_field_ident(target_len.name), rust_ident(size->name));
        o.print("}}\n");
    }
}

// Range adapters: a non-contiguous container exposed as length + indexed access,
// without assuming contiguous memory.
void emit_range_adapters(Output &o, const Projector &p) {
    for (const auto &adapter : p.m.range_adapters) {
        const int source = p.index_of(adapter.source_type);
        const amc::Function *size = p.function_by_signature(adapter.size_accessor);
        const amc::Function *at = p.function_by_signature(adapter.at_accessor);
        if (source < 0 || !size || !at) continue;
        const std::string source_name = p.name_of(adapter.source_type);
        o.print("\n// ABIX-STD range adapter: {} (length + indexed access).\n", source_name);
        o.print("pub unsafe fn {}_len(value: *const {}) -> u64 {{\n", source_name, source_name);
        o.print("    {}(value) as _\n}}\n", rust_ident(size->name));
        o.print("pub unsafe fn {}_at(value: *const {}, index: u64) -> {} {{\n", source_name, source_name,
            p.field_type(at->return_type));
        o.print("    {}(value, index as _)\n}}\n", rust_ident(at->name));
    }
}

void emit_abi_checks(Output &o, const Projector &p) {
    bool any = false;
    for (const auto &type : p.m.types) {
        if (type.kind != amc::TypeKind::RECORD || type.name.empty()) continue;
        if (!is_projectable_name(type.name)) continue;
        if (!is_rust_identifier(p.name_of(type.id))) continue;
        if (!any) {
            o.print("\n// Compile-time ABI assertions: a layout drift is a compile error.\n");
            any = true;
        }
        const std::string &name = p.name_of(type.id);
        o.print("const _: () = assert!(core::mem::size_of::<{}>() == {});\n", name, type.size);
        o.print("const _: () = assert!(core::mem::align_of::<{}>() == {});\n", name, type.align == 0 ? 1 : type.align);
        // An opaque handle is projected as a byte blob, so its fields have no
        // Rust names to assert against.
        if ((type.flags & amc::type_opaque) != 0) continue;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = p.m.fields[type.field_begin + i];
            if (field.name.empty() || (field.flags & amc::field_base) != 0) continue;
            if ((field.flags & amc::field_bitfield) != 0) continue;
            o.print("const _: () = assert!(core::mem::offset_of!({}, {}) == {});\n", name, rust_field_ident(field.name),
                field.offset);
        }
    }
}

bool emit_metadata_region(Output &o, const amc::ABIModule &m, std::string &error) {
    amc::MetadataOptions options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(m, options, region, error)) return false;
    o.print("\n// ABIX Metadata Region: manifest + desc + hash + names (offset-based).\n");
    o.print("pub const AMC_METADATA_REGION: [u8; {}] = [\n", region.size());
    for (size_t i = 0; i < region.size(); ++i) {
        if (i % 12 == 0) o.print("    ");
        o.print("0x{:02x},", region[i]);
        if (i % 12 == 11 || i + 1 == region.size()) o.print("\n");
    }
    o.print("];\n");
    o.print("pub const AMC_METADATA_REGION_SIZE: usize = {};\n", region.size());
    return true;
}

}   // namespace

std::string primitive_rust_type(const amc::Type &type) {
    // These spellings are already the exact Rust type for the recorded ABI and
    // are more precise than collapsing them to c_longlong/c_ulonglong.
    if (type.name == "isize" || type.name == "usize") return type.name;
    if (type.name == "c_long") return "core::ffi::c_long";
    if (type.name == "c_ulong") return "core::ffi::c_ulong";
    return primitive_rust_type(type.primitive_abi);
}

std::string primitive_rust_type(uint32_t primitive_abi) {
    const uint32_t kind = primitive_abi & 0xffu;
    const uint32_t width = (primitive_abi & amc::primitive_width_mask) >> amc::primitive_width_shift;
    switch (static_cast<amc::PrimitiveABIKind>(kind)) {
        case amc::PrimitiveABIKind::AMC_VOID:           return "()";
        case amc::PrimitiveABIKind::AMC_BOOL:           return "bool";
        case amc::PrimitiveABIKind::AMC_CHAR_SIGNED:
        case amc::PrimitiveABIKind::AMC_CHAR_UNSIGNED:  return "core::ffi::c_char";
        case amc::PrimitiveABIKind::AMC_SCHAR:          return "core::ffi::c_schar";
        case amc::PrimitiveABIKind::AMC_UCHAR:          return "core::ffi::c_uchar";
        case amc::PrimitiveABIKind::AMC_CHAR8:          return "u8";
        case amc::PrimitiveABIKind::AMC_CHAR16:         return "u16";
        case amc::PrimitiveABIKind::AMC_CHAR32:         return "u32";
        case amc::PrimitiveABIKind::AMC_WCHAR_SIGNED:   return width == 2 ? "i16" : "i32";
        case amc::PrimitiveABIKind::AMC_WCHAR_UNSIGNED: return width == 2 ? "u16" : "u32";
        case amc::PrimitiveABIKind::AMC_SINT:
            switch (width) {
                case 1:  return "i8";
                case 2:  return "i16";
                case 8:  return "core::ffi::c_longlong";
                case 16: return "i128";
                case 4:
                default: return "core::ffi::c_int";
            }
        case amc::PrimitiveABIKind::AMC_UINT:
            switch (width) {
                case 1:  return "u8";
                case 2:  return "u16";
                case 8:  return "core::ffi::c_ulonglong";
                case 16: return "u128";
                case 4:
                default: return "core::ffi::c_uint";
            }
        case amc::PrimitiveABIKind::AMC_FLOAT: {
            switch (static_cast<amc::FloatFormat>(
                (primitive_abi & amc::primitive_format_mask) >> amc::primitive_format_shift)) {
                case amc::FloatFormat::IEEE32:            return "core::ffi::c_float";
                case amc::FloatFormat::IEEE64:            return "core::ffi::c_double";
                case amc::FloatFormat::IEEE16:            return "u16";
                case amc::FloatFormat::BF16:              return "u16";
                case amc::FloatFormat::IEEE128:           return "u128";
                case amc::FloatFormat::X87_80:            return "u128";
                case amc::FloatFormat::PPC_DOUBLE_DOUBLE: return "u128";
                case amc::FloatFormat::NONE:
                default:                                  return "core::ffi::c_double";
            }
        }
        case amc::PrimitiveABIKind::AMC_NONE:
        default:                              return "core::ffi::c_void";
    }
}

std::string generate_projection(const amc::ABIModule &module, const amc::ir::LoweringResult *lowering) {
    Projector projector(module, lowering);
    fmt::memory_buffer buffer;
    Output o{buffer};
    o.print(AMC_GENERATED_FILE_WARNING("//"));
    o.print("// ABI: {} {} (arch={}, os={}, abi={})\n", module.package_name, module.package_version, module.arch,
        module.os, module.target_abi);
    o.print("#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code, unused_imports)]\n");
    o.print("#![allow(unexpected_cfgs, unknown_lints)]\n");
    o.print("use core::ffi;\n\n");
    o.print("pub const ABIX_PACKAGE_NAME: &str = {};\n", rust_string(module.package_name));
    o.print("pub const ABIX_PACKAGE_VERSION: &str = {};\n", rust_string(module.package_version));
    o.print("/// Library expected to own the exported symbols; used by `#[link]` under `abix_link`.\n");
    o.print("pub const ABIX_LIBRARY_NAME: &str = {};\n", rust_string(module.package_name));

    for (const auto &type : module.types) {
        if (amc::lang::cancellation_requested()) return {};
        o.print("\n");
        // A consumer must not reconstruct an STL/template/reference type; emit
        // it as an opaque blob instead of a guessed Rust struct.
        if (!is_projectable_name(type.name)) {
            emit_opaque(o, projector, type);
            continue;
        }
        emit_type(o, projector, type);
    }
    emit_functions(o, projector);
    emit_owned(o, projector);
    emit_owner_views(o, projector);
    emit_capability_contracts(o, projector);
    emit_interface_proxies(o, projector);
    emit_error_sets(o, projector);
    emit_errors(o, projector);
    emit_lowerings(o, projector);
    emit_view_adapters(o, projector);
    emit_range_adapters(o, projector);
    emit_abi_checks(o, projector);
    std::string error;
    if (!emit_metadata_region(o, module, error)) fmt::print(stderr, "{}\n", error);

    // A zero-allocation reader for the embedded ABIX Metadata Region. Emitted as a
    // nested `metadata` module so a consumer can inspect the ABI facts the library
    // was described with (type ids, layout hashes, sizes, calling conventions)
    // without any C++ toolchain and without parsing `.abix` offline.

    o.print("{}", amc::backend::read_codein_text("Rust/MetaData.rs"));
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
    amc::ABIModule module = std::move(artifact.abi);
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

}   // namespace skl::amc::lang::rust
