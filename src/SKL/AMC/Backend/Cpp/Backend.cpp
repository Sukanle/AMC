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
#include "SKL/AMC/Backend/Cpp/Cpp.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Lang/Lang.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Core/Metadata.h"

#include <fmt/os.h>

#include <set>

#if defined(__APPLE__)
// Used by the macOS helpers below to derive the Homebrew LLVM prefix from the
// Clang resource directory; the lightweight provider header no longer drags
// these in.
#  include <llvm/ADT/SmallString.h>
#  include <llvm/Support/Path.h>
#  include <unistd.h>
#endif

#ifndef ABIX_CLANG_RESOURCE_DIR
#  define ABIX_CLANG_RESOURCE_DIR ""
#endif

namespace amc::lang::cpp {

const char * const kClangResourceDir = ABIX_CLANG_RESOURCE_DIR;

static std::string cpp_name(std::string s) {
    for (size_t p = 0; (p = s.find("::", p)) != std::string::npos; s.replace(p, 2, "_")) {}
    for (char &c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
    return s;
}

static bool is_cpp_identifier(const std::string &s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

// A name that can be pasted into a generated `sizeof`/`offsetof` check: a C++
// identifier, optionally qualified with `::`. This deliberately rejects the
// mangled names AMC gives to implicit/unnamed types (`struct Foo *`, anonymous
// enums) so the ABI check header never emits uncompilable code.
static bool is_cpp_qualified_name(const std::string &s) {
    if (s.empty() || s == "void") return false;
    size_t i = 0;
    while (i < s.size()) {
        if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) return false;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
            ++i;
        if (i == s.size()) return true;
        if (s.compare(i, 2, "::") != 0) return false;
        i += 2;
        if (i == s.size()) return false;   // trailing `::`
    }
    return true;
}

static std::string cpp_string(const std::string &value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else
            result += c;
    }
    result += '"';
    return result;
}

static std::string hex_u64(uint64_t value) { return fmt::format("0x{:x}ULL", value); }

// Render a recorded TypeID as a C++ spelling for a generated interface method
// typedef. Pointer and record names are already source-level spellings; a C
// primitive that carries the C spelling `_Bool` maps to the C++ `bool`, exactly
// as the generated thunk source does.
static std::string cpp_type_of(const amc::AbiModule &m, amc::Hash128 id) {
    if (id.lo == 0 && id.hi == 0) return "void";
    for (const auto &type : m.types)
        if (type.id == id) return type.name == "_Bool" ? std::string("bool") : type.name;
    return "void";
}

static bool type_is_pointer(const amc::AbiModule &m, amc::Hash128 id) {
    for (const auto &type : m.types)
        if (type.id == id) return type.kind == amc::TypeKind::pointer;
    return false;
}

// An interface method is usable as an object-handle call only when its first
// parameter is a pointer (the object handle). The `[[interface]]` declaration
// references whatever function defines the contract, which is not necessarily an
// object method, so the generated proxy class is emitted only when every method
// qualifies.
static bool interface_has_handle_methods(const amc::AbiModule &m, const amc::InterfaceRecord &record) {
    if (record.method_count == 0) return false;
    for (uint32_t i = 0; i < record.method_count; ++i) {
        const auto &method = m.interface_methods[record.method_begin + i];
        if (method.parameters.empty() || !type_is_pointer(m, method.parameters[0].type_id)) return false;
        // The generated proxy forwards the runtime's `void *` object handle to
        // the typed function pointer. A typed handle (e.g. `const DemoVec2 *`)
        // would not implicitly convert; such contracts remain metadata-only
        // until an explicit void-pointer adapter is exposed.
        const std::string first = cpp_type_of(m, method.parameters[0].type_id);
        if (first != "void*" && first != "const void*") return false;
    }
    return true;
}

int backend(const char *input, const char *output) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::ir::Module artifact;
    std::string e;
    if (!amc::ir::load_artifact(input, artifact, e)) {
        fmt::print(stderr, "{}\n", e);
        return 1;
    }
    amc::AbiModule m = std::move(artifact.abi);
    amc::ir::LoweringResult lowering = std::move(artifact.lowering);
    std::string lowering_error;
    if (!amc::ir::apply_lowering_contracts(lowering, m, lowering_error)) {
        fmt::print(stderr, "{}\n", lowering_error);
        return 1;
    }
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    auto o = fmt::output_file(output);
    o.print("#pragma once\n");
    o.print(AMC_GENERATED_FILE_WARNING("//"));
    o.print("// Generated by `amc generate -l cpp`.\n");
    o.print(
        "#include <SKL/ABIX/Runtime/Registry.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <string.h>\n");
    // The interface contract constants and typed signature table below name
    // `InterfaceId` / `mics::util::array`, so pull in those headers only when the
    // module actually declares interfaces; the proxy API is added only when a
    // proxy class is actually generated.
    bool any_interface_proxy = false;
    for (const auto &record : m.interfaces)
        if (interface_has_handle_methods(m, record)) {
            any_interface_proxy = true;
            break;
        }
    if (!m.interfaces.empty()) {
        o.print("#include <SKL/MICS/Util/Array.h>\n");
        o.print("#include <SKL/ABIX/Interface/Contract.h>\n");
    }
    if (any_interface_proxy) o.print("#include <SKL/ABIX/Interface/Proxy.h>\n");
    o.print(
        "namespace amc_generated {{\n"
        "using TypeId = ::skl::abix::model::TypeId;\n"
        "using LayoutInfo = ::skl::abix::metadata::LayoutInfo;\n"
        "using FieldInfo = ::skl::abix::metadata::FieldDescriptor;\n"
        "using ParameterInfo = ::skl::abix::metadata::ParameterDescriptor;\n"
        "using FunctionInfo = ::skl::abix::metadata::FunctionDescriptor;\n"
        "using SymbolInfo = ::skl::abix::metadata::SymbolDescriptor;\n"
        "using ModuleInfo = ::skl::abix::metadata::ModuleDescriptor;\n"
        "enum class MapOpcode : uint8_t {{ \n"
        "    copy_field,\n"
        "    convert_int,\n"
        "    convert_float,\n"
        "    add_default,\n"
        "    skip_field,\n"
        "}};\n"
        "struct MapOperation {{\n"
        "    MapOpcode opcode;\n"
        "    uint32_t source_field;\n"
        "    uint32_t target_field;\n"
        "    uint32_t source_offset;\n"
        "    uint32_t target_offset;\n"
        "    uint32_t byte_count;\n"
        "    TypeId auxiliary;\n"
        "}};\n"
        "template <typename Source, typename Target> struct MapPrivate;\n"
        "template <typename T> struct TypeTraits;\n"
        "template <typename T, typename E> struct Result {{\n"
        "    bool ok = false;\n"
        "    T value{{}};\n"
        "    E error{{}};\n"
        "    explicit operator bool() const noexcept {{ return ok; }}\n"
        "}};\n");

    for (const auto &type : m.types) {
        if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        o.print("struct {} {{\n", id);
        o.print("    static constexpr TypeId type_id {{{}, {}}};\n", hex_u64(type.id.lo), hex_u64(type.id.hi));
        o.print("    static constexpr uint64_t type_id_lo = {};\n", hex_u64(type.id.lo));
        o.print("    static constexpr uint64_t type_id_hi = {};\n", hex_u64(type.id.hi));
        o.print("    static constexpr LayoutInfo layout {{{}, {}, {}}};\n", type.size, type.align, type.field_count);
        o.print("    static constexpr size_t size = {};\n", type.size);
        o.print("    static constexpr size_t align = {};\n", type.align);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            o.print("    static constexpr size_t {}_offset = {};\n", cpp_name(field.name), field.offset);
        }
        o.print("}};\n");
        o.print("template <> struct TypeTraits<{}> {{\n", id);
        o.print("    static constexpr TypeId type_id {{{}, {}}};\n", hex_u64(type.id.lo), hex_u64(type.id.hi));
        o.print("    static constexpr LayoutInfo layout {{{}, {}, {}}};\n", type.size, type.align, type.field_count);
        o.print("    static constexpr size_t size = {};\n", type.size);
        o.print("    static constexpr size_t align = {};\n", type.align);
        o.print("}};\n");
        o.print("inline constexpr FieldInfo {}_fields[{}] = {{\n", id, type.field_count == 0 ? 1 : type.field_count);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = m.fields[type.field_begin + i];
            o.print("    {{{{{}, {}}}, {}, {}}},\n", hex_u64(field.type_id.lo), hex_u64(field.type_id.hi), field.offset,
                field.flags);
        }
        o.print("}};\n");
    }

    for (size_t i = 0; i < m.functions.size(); ++i) {
        const auto &function = m.functions[i];
        const auto param_count = function.parameters.empty() ? 1 : function.parameters.size();
        o.print("inline constexpr ParameterInfo amc_function_{}_parameters[{}] = {{\n", i, param_count);
        for (const auto &parameter : function.parameters)
            o.print("    {{{{{}, {}}}, {}}},\n", hex_u64(parameter.type_id.lo), hex_u64(parameter.type_id.hi),
                parameter.flags);
        o.print("}};\n");
        o.print(
            "inline constexpr FunctionInfo amc_function_{} {{amc_function_{}_parameters, {{{}, {}}},"
            " {{{}, {}}}, {}, {}, {}}};\n",
            i, i, hex_u64(function.signature.lo), hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
            hex_u64(function.return_type.hi), function.parameters.size(), function.calling_convention, function.flags);
    }

    {
        const auto type_count = m.types.empty() ? 1 : m.types.size();
        o.print("inline constexpr ::skl::abix::metadata::TypeDescriptor amc_types[{}] = {{\n", type_count);
        for (const auto &type : m.types) {
            const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
            o.print("    {{{}_fields, {{{}, {}}}, {{{}, {}}}, {}, {}, {}, {}}},\n", id, hex_u64(type.id.lo),
                hex_u64(type.id.hi), hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi), type.flags, type.size,
                type.align, type.field_count);
        }
        o.print("}};\n");
    }

    // Canonical TypeDesc / TypeLayout arrays — the one ABI truth.
    // These are imported directly by RuntimeRegistry instead of being
    // reconstructed from the runtime projection (amc_types[]).
    {
        const auto type_count = m.types.empty() ? 1 : m.types.size();
        o.print("inline constexpr ::skl::abix::model::TypeDesc amc_canonical_types[{}] = {{\n", type_count);
        for (size_t i = 0; i < m.types.size(); ++i) {
            const auto &type = m.types[i];
            // name_offset = 0 (names live in .abix.names section for
            // diagnostic use only — stripped from the runtime registry).
            o.print("    {{{{{}, {}}}, {}, {}, {}}},\n", hex_u64(type.id.lo), hex_u64(type.id.hi), type.flags, 0U,
                static_cast<uint32_t>(i));
        }
        if (m.types.empty()) {
            o.print("    {{{{{}, {}}}, {}, {}, {}}},\n", "0ULL", "0ULL", 0U, 0U, 0U);
        }
        o.print("}};\n");

        o.print("inline constexpr ::skl::abix::model::TypeLayout amc_canonical_layouts[{}] = {{\n", type_count);
        for (const auto &type : m.types) {
            o.print("    {{{}, {}, {}, {}, {{{}, {}}}}},\n", type.size, type.align, type.field_begin, type.field_count,
                hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi));
        }
        if (m.types.empty()) {
            o.print("    {{{}, {}, {}, {}, {{{}, {}}}}},\n", 0U, 0U, 0U, 0U, "0ULL", "0ULL");
        }
        o.print("}};\n");
    }

    {
        const auto func_count = m.functions.empty() ? 1 : m.functions.size();
        o.print("inline constexpr ::skl::abix::metadata::FunctionDescriptor amc_functions[{}] = {{\n", func_count);
        for (size_t i = 0; i < m.functions.size(); ++i) {
            const auto &function = m.functions[i];
            o.print("    {{amc_function_{}_parameters, {{{}, {}}}, {{{}, {}}}, {}, {}, {}}},\n", i,
                hex_u64(function.signature.lo), hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
                hex_u64(function.return_type.hi), function.parameters.size(), function.calling_convention,
                function.flags);
        }
        o.print("}};\n");
    }

    {
        const auto sym_count = m.symbols.empty() ? 1 : m.symbols.size();
        o.print("inline constexpr SymbolInfo amc_symbols[{}] = {{\n", sym_count);
        for (const auto &symbol : m.symbols)
            o.print("    {{{}, {}}},\n", static_cast<uint32_t>(symbol.kind), symbol.target_index);
        o.print("}};\n");
    }

    // Struct layout: name, version, types, canonical_types, canonical_layouts,
    // type_count, function_count, functions, symbols, symbol_count.
    o.print(
        "inline constexpr ModuleInfo amc_module{{{}, {}, amc_types, amc_canonical_types, amc_canonical_layouts, {}, "
        "{}, amc_functions, amc_symbols, {}}};\n",
        cpp_string(m.package_name), cpp_string(m.package_version), m.types.size(), m.functions.size(),
        m.symbols.size());

    // Interface contracts (`[[interface]]` / `[[implements]]`): a named, versioned
    // contract, not a private vtable. For each contract the projection emits its
    // identity, version, adapter typedefs and a `Traits` struct with a typed
    // zero-based slot table (`Traits::slots.<method>`) plus the expected
    // signatures. When every method takes the object handle first it also emits
    // a proxy class derived from `UniqueInterfaceProxy<Traits>` — so a consumer can
    // bind, verify and call through a header-declared interface without touching
    // the provider's layout.
    for (const auto &record : m.interfaces) {
        const std::string name = cpp_name(record.name);
        // Methods addressed by slot, so a non-contiguous table is still emitted
        // in slot order.
        std::vector<const amc::InterfaceMethod *> by_slot;
        for (uint32_t i = 0; i < record.method_count; ++i) {
            const auto &method = m.interface_methods[record.method_begin + i];
            if (method.slot >= by_slot.size()) by_slot.resize(method.slot + 1, nullptr);
            by_slot[method.slot] = &method;
        }
        o.print("\n// Interface contract `{}` (version {}).\n", record.name, record.version);
        o.print("inline constexpr ::skl::abix::poly::InterfaceId k{}Interface{{{}, {}}};\n", name,
            hex_u64(record.id.lo), hex_u64(record.id.hi));
        o.print("inline constexpr uint32_t k{}Version = {};\n", name, record.version);
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            std::string params;
            for (const auto &parameter : method->parameters) {
                if (!params.empty()) params += ", ";
                params += cpp_type_of(m, parameter.type_id);
            }
            o.print("using {}_{}_fn = {} (*)({});\n", name, cpp_name(method->name), cpp_type_of(m, method->return_type),
                params);
        }

        // Traits: the contract facts a proxy needs. Signatures double as the slot
        // capacity, so a sparse table keeps a zero hash for every unbound slot.
        o.print("struct {}Traits {{\n", name);
        if (by_slot.empty()) {
            o.print(
                "    static constexpr ::skl::mics::util::array<::skl::abix::poly::InterfaceMethodHash, 0> "
                "signatures = {{}};\n");
        } else {
            o.print("    static constexpr ::skl::mics::util::array signatures = {{\n");
            for (const auto *method : by_slot) {
                if (method == nullptr)
                    o.print("        ::skl::abix::poly::InterfaceMethodHash{{0ULL, 0ULL}},\n");
                else
                    o.print("        ::skl::abix::poly::InterfaceMethodHash{{{}, {}}},\n",
                        hex_u64(method->signature.lo), hex_u64(method->signature.hi));
            }
            o.print("    }};\n");
        }
        o.print("    static constexpr uint32_t version = k{}Version;\n", name);
        o.print("    static constexpr ::skl::abix::poly::InterfaceId id = k{}Interface;\n", name);
        o.print("    struct alignas(8) Slots {{\n");
        for (size_t slot = 0; slot < by_slot.size(); ++slot) {
            const auto *method = by_slot[slot];
            if (method != nullptr)
                o.print("        uint32_t {};\n", cpp_name(method->name));
            else
                o.print("        uint32_t reserved_slot_{};\n", slot);
        }
        o.print("    }};\n");
        o.print(
            "    alignas(8) static constexpr Slots slots = "
            "::skl::mics::util::make_iota<Slots, signatures.size(), 0>();\n");
        o.print("}};\n");

        if (!interface_has_handle_methods(m, record)) {
            o.print("// No proxy class: a method does not take the object handle as its first parameter.\n");
            continue;
        }
        o.print(
            "class {} : public ::skl::abix::poly::UniqueInterfaceProxy<{}Traits> {{\n"
            "    using Base = ::skl::abix::poly::UniqueInterfaceProxy<{}Traits>;\n\n"
            "public:\n"
            "    using Base::Base;\n",
            name, name, name);
        for (const auto *method : by_slot) {
            if (method == nullptr) continue;
            const std::string mname = cpp_name(method->name);
            const std::string ret = cpp_type_of(m, method->return_type);
            std::string decl, forward;
            for (size_t p = 1; p < method->parameters.size(); ++p) {
                const std::string arg = fmt::format("arg{}", p - 1);
                if (!decl.empty()) {
                    decl += ", ";
                    forward += ", ";
                }
                decl += cpp_type_of(m, method->parameters[p].type_id) + " " + arg;
                forward += arg;
            }
            if (forward.empty())
                o.print(
                    "    {} {}({}) const noexcept {{ return this->template call<{}_{}_fn>({}Traits::slots.{}); }}\n",
                    ret, mname, decl, name, mname, name, mname);
            else
                o.print(
                    "    {} {}({}) const noexcept {{ return this->template "
                    "call<{}_{}_fn>({}Traits::slots.{}, {}); }}\n",
                    ret, mname, decl, name, mname, name, mname, forward);
        }
        o.print("}};\n");
    }

    // .abix.names section: type/field/function names for diagnostic use only.
    // This section is NOT referenced by any runtime code and can be safely
    // stripped via: strip --remove-section=.abix.names <binary>
    // External tools (amc-dump, ABIX symbol server) read this section from
    // the binary or from a companion .abix file archived at build time.
    //
    // `used` is required: without it the compiler is free to drop this
    // unreferenced variable entirely, which would silently make the names
    // un-strippable because they were never emitted.
    //
    // Section attributes differ per container: ELF uses the dot-separated
    // spelling; Mach-O requires a segment and a <=16-byte name; PE section names
    // are 8 bytes and MSVC needs `#pragma section` + `__declspec(allocate)`
    // instead of an attribute.
    if (!m.types.empty()) {
        o.print(
            "#if defined(__APPLE__)\n"
            "#  define SKL_ABIX_NAMES_DECL "
            "__attribute__((used, section(\"__DATA,__abix_names\"))) inline constexpr const char *amc_type_names[]\n"
            "#elif defined(_WIN32) && defined(_MSC_VER)\n"
            "#  pragma section(\".abix.n\", read)\n"
            "#  define SKL_ABIX_NAMES_DECL "
            "extern \"C\" __declspec(selectany) __declspec(allocate(\".abix.n\")) const char *const amc_type_names[]\n"
            "#elif defined(_WIN32)\n"
            "#  define SKL_ABIX_NAMES_DECL "
            "__attribute__((used, section(\".abix.n\"))) inline constexpr const char *amc_type_names[]\n"
            "#else\n"
            "#  define SKL_ABIX_NAMES_DECL "
            "__attribute__((used, section(\".abix.names\"))) inline constexpr const char *amc_type_names[]\n"
            "#endif\n"
            "SKL_ABIX_NAMES_DECL = {{\n");
        for (const auto &type : m.types)
            o.print("    {},\n", cpp_string(type.name));
        o.print(
            "}};\n"
            "#if defined(_WIN32) && defined(_MSC_VER)\n"
            "// selectany + /INCLUDE keep the section past /OPT:REF despite being unreferenced.\n"
            "#  pragma comment(linker, \"/INCLUDE:amc_type_names\")\n"
            "#endif\n");
    }

    // ABIX Metadata Region (AI-PM): the self-describing, pointer-free image of
    // this module (manifest + desc + hash + names). It lives in its own section
    // so an offline tool can locate the whole region through the container's
    // section table (ELF or Mach-O) without loading the program. The C++
    // descriptors above are its runtime projection and remain the hot path.
    {
        amc::MetadataOptions metadata_options;
        std::vector<uint8_t> region;
        std::string metadata_error;
        if (!amc::build_metadata_region(m, metadata_options, region, metadata_error)) {
            fmt::print(stderr, "{}\n", metadata_error);
            return 1;
        }
        const uint32_t region_size_value = static_cast<uint32_t>(region.size());
        o.print(
            "\n// ABIX Metadata Region: manifest + desc + hash + names (offset-based).\n"
            "#if defined(__APPLE__)\n"
            "#  define SKL_ABIX_METADATA_DECL "
            "__attribute__((used, section(\"__DATA,__abix_metadata\"), aligned(8))) inline constexpr unsigned char "
            "amc_metadata_region[]\n"
            "#elif defined(_WIN32) && defined(_MSC_VER)\n"
            "#  pragma section(\".abix\", read)\n"
            "#  define SKL_ABIX_METADATA_DECL "
            "extern \"C\" __declspec(selectany) __declspec(allocate(\".abix\")) const unsigned char "
            "amc_metadata_region[]\n"
            "#elif defined(_WIN32)\n"
            "#  define SKL_ABIX_METADATA_DECL "
            "__attribute__((used, section(\".abix\"), aligned(8))) inline constexpr unsigned char "
            "amc_metadata_region[]\n"
            "#else\n"
            "#  define SKL_ABIX_METADATA_DECL "
            "__attribute__((used, section(\".abix.metadata\"), aligned(8))) inline constexpr unsigned char "
            "amc_metadata_region[]\n"
            "#endif\n"
            "SKL_ABIX_METADATA_DECL = {{\n"
            "#if defined(_WIN32)\n"
            "    // AbixPeHeader (24 B): magic, version, size, flags, region_offset, region_size, reserved.\n"
            "    0x41,0x42,0x49,0x58, 0x01,0x00, 0x18,0x00, 0x00,0x00,0x00,0x00,\n"
            "    0x18,0x00,0x00,0x00, 0x{:02x},0x{:02x},0x{:02x},0x{:02x}, 0x00,0x00,0x00,0x00,\n"
            "#endif\n",
            region_size_value & 0xff, (region_size_value >> 8) & 0xff, (region_size_value >> 16) & 0xff,
            (region_size_value >> 24) & 0xff);
        for (size_t i = 0; i < region.size(); ++i) {
            if (i % 12 == 0) o.print("    ");
            o.print("0x{:02x},", region[i]);
            if (i % 12 == 11 || i + 1 == region.size()) o.print("\n");
        }
        o.print(
            "}};\n"
            "#if defined(_WIN32) && defined(_MSC_VER)\n"
            "// selectany + /INCLUDE keep the section past /OPT:REF despite being unreferenced.\n"
            "#  pragma comment(linker, \"/INCLUDE:amc_metadata_region\")\n"
            "#endif\n");
        o.print("inline constexpr size_t amc_metadata_region_size = sizeof(amc_metadata_region);\n");
    }

    for (size_t i = 0; i < m.maps.size(); ++i) {
        const auto &map = m.maps[i];
        const auto op_count = map.operations.empty() ? 1 : map.operations.size();
        o.print("inline constexpr MapOperation amc_map_{}_operations[{}] = {{\n", i, op_count);
        for (const auto &operation : map.operations) {
            const char *opname = operation.opcode == amc::MapOpcode::copy_field    ? "copy_field"
                               : operation.opcode == amc::MapOpcode::convert_int   ? "convert_int"
                               : operation.opcode == amc::MapOpcode::convert_float ? "convert_float"
                               : operation.opcode == amc::MapOpcode::add_default   ? "add_default"
                                                                                   : "skip_field";
            o.print("    {{MapOpcode::{}, {}, {}, {}, {}, {}, {{{}, {}}}}},\n", opname, operation.source_field,
                operation.target_field, operation.source_offset, operation.target_offset, operation.byte_count,
                hex_u64(operation.auxiliary.lo), hex_u64(operation.auxiliary.hi));
        }
        o.print(
            "}};\n"
            "template <> struct MapPrivate<{}_ABIX, {}_ABIX> {{\n"
            "  static constexpr const MapOperation *operations = amc_map_{}_operations;\n"
            "  static constexpr size_t operation_count = {};\n"
            "  static bool apply(void *target, const void *source) noexcept {{\n"
            "    if (!target || !source) return false;\n",
            cpp_name(map.source_name), cpp_name(map.target_name), i, map.operations.size());
        for (const auto &operation : map.operations) {
            switch (operation.opcode) {
                case amc::MapOpcode::copy_field:
                    o.print(
                        "    memcpy(static_cast<char *>(target) + {}, static_cast<const char *>(source) + {}, "
                        "{});\n",
                        operation.target_offset, operation.source_offset, operation.byte_count);
                    break;
                case amc::MapOpcode::add_default:
                    o.print("    memset(static_cast<char *>(target) + {}, 0, {});\n", operation.target_offset,
                        operation.byte_count);
                    break;
                case amc::MapOpcode::skip_field:  break;
                case amc::MapOpcode::convert_int:
                case amc::MapOpcode::convert_float:
                    o.print("    return false;  // conversion requires an explicit native converter\n");
                    break;
            }
        }
        o.print(
            "    return true;\n"
            "}}\n"
            "}};\n");
    }

    // Exposed-handle wrappers: a move-only RAII class around the create/destroy
    // thunks plus typed method calls. The method list and signatures come from
    // the lowering contract, never from parsing the thunk names.
    for (const auto &record : lowering.exposed) {
        if (record.status != amc::ir::LoweringStatus::lowered) continue;
        const auto find_function = [&](amc::Hash128 signature) -> const amc::Function * {
            if (signature.lo == 0 && signature.hi == 0) return nullptr;
            for (const auto &function : m.functions)
                if (function.signature == signature) return &function;
            return nullptr;
        };
        const amc::Function *create = find_function(record.create);
        const amc::Function *destroy = find_function(record.destroy);
        if (create == nullptr && destroy == nullptr && record.methods.empty()) continue;
        const std::string handle = cpp_name(record.name) + "Handle";
        std::string self_type = "void*";
        if (create != nullptr)
            self_type = cpp_type_of(m, create->return_type);
        else if (destroy != nullptr && !destroy->parameters.empty())
            self_type = cpp_type_of(m, destroy->parameters[0].type_id);
        else
            for (const auto &method : record.methods) {
                const amc::Function *function = find_function(method.abi);
                if (function != nullptr && !function->parameters.empty()) {
                    self_type = cpp_type_of(m, function->parameters[0].type_id);
                    break;
                }
            }
        const auto emit_decl = [&](const amc::Function *function) {
            if (function == nullptr) return;
            std::string params;
            for (size_t i = 0; i < function->parameters.size(); ++i) {
                const auto &parameter = function->parameters[i];
                if (i != 0) params += ", ";
                params += cpp_type_of(m, parameter.type_id);
            }
            o.print("{} {}({});\n", cpp_type_of(m, function->return_type), function->name, params);
        };
        o.print("\nextern \"C\" {{\n");
        emit_decl(create);
        emit_decl(destroy);
        for (const auto &method : record.methods)
            emit_decl(find_function(method.abi));
        o.print("}}\n");
        o.print("\n// RAII handle for the exposed `{}` capability.\n", record.name);
        o.print("class {} {{\npublic:\n", handle);
        o.print("    {}() = default;\n", handle);
        o.print("    explicit {}({} ptr) : ptr_(ptr) {{}}\n", handle, self_type);
        o.print("    {}(const {} &) = delete;\n", handle, handle);
        o.print("    {} &operator=(const {} &) = delete;\n", handle, handle);
        o.print("    {}({} &&other) noexcept : ptr_(other.ptr_) {{ other.ptr_ = nullptr; }}\n", handle, handle);
        o.print("    {} &operator=({} &&other) noexcept {{\n", handle, handle);
        o.print("        if (this != &other) {{ reset(); ptr_ = other.ptr_; other.ptr_ = nullptr; }}\n");
        o.print("        return *this;\n    }}\n");
        o.print("    ~{}() {{ reset(); }}\n", handle);
        if (destroy != nullptr) {
            o.print("    void reset() {{\n");
            o.print("        if (ptr_ != nullptr) {{ {}(ptr_); ptr_ = nullptr; }}\n", destroy->name);
            o.print("    }}\n");
        } else {
            o.print("    void reset() {{ ptr_ = nullptr; }}\n");
        }
        o.print("    {} native_handle() const {{ return ptr_; }}\n", self_type);
        o.print("    explicit operator bool() const {{ return ptr_ != nullptr; }}\n");
        if (create != nullptr) {
            std::string params, args;
            for (size_t i = 0; i < create->parameters.size(); ++i) {
                const auto &parameter = create->parameters[i];
                const std::string name = parameter.name.empty() ? fmt::format("arg{}", i) : cpp_name(parameter.name);
                if (i != 0) {
                    params += ", ";
                    args += ", ";
                }
                params += cpp_type_of(m, parameter.type_id) + " " + name;
                args += name;
            }
            o.print("    static {} create({}) {{ return {}({}({})); }}\n", handle, params, handle, create->name, args);
        }
        for (const auto &method : record.methods) {
            const amc::Function *function = find_function(method.abi);
            if (function == nullptr || function->parameters.empty()) continue;
            const std::string name = cpp_name(method.name);
            if (name == "create" || name == "reset" || name == "native_handle") continue;
            const int return_index = [&] {
                for (size_t i = 0; i < m.types.size(); ++i)
                    if (m.types[i].id == function->return_type) return static_cast<int>(i);
                return -1;
            }();
            const bool returns_void = return_index >= 0
                                   && m.types[static_cast<size_t>(return_index)].kind == amc::TypeKind::primitive
                                   && (m.types[static_cast<size_t>(return_index)].primitive_abi & 0xffu)
                                          == static_cast<uint32_t>(amc::PrimitiveAbiKind::void_);
            std::string params, args;
            for (size_t i = 1; i < function->parameters.size(); ++i) {
                const auto &parameter = function->parameters[i];
                const std::string parameter_name =
                    parameter.name.empty() ? fmt::format("arg{}", i - 1) : cpp_name(parameter.name);
                if (i != 1) params += ", ";
                params += cpp_type_of(m, parameter.type_id) + " " + parameter_name;
                args += (i == 1 ? "" : ", ") + parameter_name;
            }
            if (returns_void)
                o.print("    void {}({}) const {{ {}(ptr_{}); }}\n", name, params, function->name,
                    args.empty() ? "" : ", " + args);
            else
                o.print("    {} {}({}) const {{ return {}(ptr_{}); }}\n", cpp_type_of(m, function->return_type), name,
                    params, function->name, args.empty() ? "" : ", " + args);
        }
        o.print("private:\n    {} ptr_ = nullptr;\n}};\n", self_type);
    }

    // Named error sets: stable numeric codes, names and generic conditions.
    // Unknown values are preserved by carrying the raw status in `value`.
    for (const auto &set : m.error_sets) {
        const std::string name = cpp_name(set.name);
        o.print(
            "\n// Named error set `{}` (domain 0x{:016x}).\n", set.name, static_cast<unsigned long long>(set.domain));
        o.print("struct {} {{\n", name);
        o.print("    int value = 0;\n");
        o.print("    constexpr {}() = default;\n", name);
        o.print("    constexpr explicit {}(int raw) : value(raw) {{}}\n", name);
        o.print(
            "    static constexpr unsigned long long domain = 0x{:x};\n", static_cast<unsigned long long>(set.domain));
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = m.error_codes[set.code_begin + i];
            o.print("    static constexpr int {} = {};\n", cpp_name(code.name), code.value);
        }
        o.print("    static constexpr const char *name(int raw) noexcept {{\n        switch (raw) {{\n");
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = m.error_codes[set.code_begin + i];
            o.print("            case {}: return \"{}\";\n", code.value, code.name);
        }
        o.print("            default: return \"unknown\";\n        }}\n    }}\n");
        o.print("    static constexpr int condition(int raw) noexcept {{\n        switch (raw) {{\n");
        for (uint32_t i = 0; i < set.code_count; ++i) {
            const auto &code = m.error_codes[set.code_begin + i];
            o.print("            case {}: return {};\n", code.value, code.condition);
        }
        o.print("            default: return -1;\n        }}\n    }}\n");
        o.print(
            "    static constexpr const char *condition_name(int raw) noexcept {{\n        switch (condition(raw)) "
            "{{\n");
        std::set<int32_t> conditions;
        for (uint32_t i = 0; i < set.code_count; ++i)
            conditions.insert(m.error_codes[set.code_begin + i].condition);
        for (const int32_t condition : conditions)
            o.print("            case {}: return \"{}\";\n", condition,
                amc::generic_condition_name(static_cast<amc::GenericCondition>(condition)));
        o.print("            default: return \"unknown\";\n        }}\n    }}\n");
        o.print("}};\n");
    }

    // Error-boundary wrappers: turn the declared C status into a typed
    // `Result<T, E>` instead of exposing a bare status code.
    for (const auto &record : m.errors) {
        if (record.function_index >= m.functions.size()) continue;
        const amc::Function &function = m.functions[record.function_index];
        const auto find_type = [&](amc::Hash128 id) -> const amc::Type * {
            for (const auto &type : m.types)
                if (type.id == id) return &type;
            return nullptr;
        };
        const auto pointee_of = [&](amc::Hash128 id) -> amc::Hash128 {
            for (const auto &ref : m.pointees)
                if (ref.pointer == id) return ref.pointee;
            return {};
        };
        const auto parameter_name = [](const amc::Parameter &parameter, size_t index) {
            return parameter.name.empty() ? fmt::format("arg{}", index) : cpp_name(parameter.name);
        };

        const auto find_error_set = [&](amc::Hash128 id) -> const amc::ErrorSet * {
            if (id.lo == 0 && id.hi == 0) return nullptr;
            for (const auto &set : m.error_sets)
                if (set.id == id) return &set;
            return nullptr;
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
            const auto &status_field = m.fields[result->field_begin + record.status_parameter];
            const auto &value_field = m.fields[result->field_begin + record.value_parameter];
            value_type = cpp_type_of(m, value_field.type_id);
            status_type = cpp_type_of(m, status_field.type_id);
            status_access = cpp_name(status_field.name);
            value_access = cpp_name(value_field.name);
            for (size_t i = 0; i < function.parameters.size(); ++i) {
                if (i != 0) {
                    params += ", ";
                    call_args += ", ";
                }
                const std::string name = parameter_name(function.parameters[i], i);
                params += cpp_type_of(m, function.parameters[i].type_id) + " " + name;
                call_args += name;
            }
        } else if (record.status_source == amc::StatusSource::return_value) {
            if (record.value_parameter == UINT32_MAX || record.value_parameter >= function.parameters.size()) continue;
            const amc::Hash128 value_id = pointee_of(function.parameters[record.value_parameter].type_id);
            if (value_id.lo == 0 && value_id.hi == 0) continue;
            value_type = cpp_type_of(m, value_id);
            status_type = cpp_type_of(m, function.return_type);
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
                params += cpp_type_of(m, function.parameters[i].type_id) + " " + name;
                call_args += name;
            }
        } else {
            continue;
        }

        const amc::ErrorSet *error_set = find_error_set(record.error_set);
        const std::string error_type = error_set != nullptr ? cpp_name(error_set->name) : status_type;
        o.print("\n// Typed error boundary for `{}`.\n", function.name);
        o.print("inline Result<{}, {}> {}_result({}) {{\n", value_type, error_type, function.name, params);
        if (record.status_source == amc::StatusSource::return_value) o.print("    {} value{{}};\n", value_type);
        if (record.status_source == amc::StatusSource::return_field)
            o.print("    auto raw = {}({});\n", function.name, call_args);
        else
            o.print("    auto status = {}({});\n", function.name, call_args);
        if (record.status_source == amc::StatusSource::return_field)
            o.print("    if (raw.{} == static_cast<{}>({})) return {{true, raw.{}, {{}}}};\n", status_access,
                status_type, record.success_value, value_access);
        else
            o.print("    if (status == static_cast<{}>({})) return {{true, value, {{}}}};\n", status_type,
                record.success_value);
        if (record.status_source == amc::StatusSource::return_field)
            o.print("    return {{false, {{}}, {}(raw.{})}};\n", error_type, status_access);
        else
            o.print("    return {{false, {{}}, {}(status)}};\n", error_type);
        o.print("}}\n");
    }

    o.print(
        "}}  // namespace amc_generated\n"
        "#ifdef AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n"
        "namespace skl::abix::runtime {{\n");
    for (const auto &type : m.types) {
        if (type.kind != amc::TypeKind::record
            || (type.flags & amc::type_template_primary) != 0
            || type.name.find('<') != std::string::npos
            || type.name.rfind("std::", 0) == 0)
            continue;
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        o.print(
            "template <> struct TypeTraits<::{}> {{\n"
            "  static constexpr ::skl::abix::model::TypeId type_id = ::amc_generated::{}::type_id;\n"
            "}};\n",
            type.name, id);
    }
    o.print(
        "}}  // namespace skl::abix::runtime\n"
        "#endif  // AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n");

    // ABI check header (AI-P2 clangd integration): compile-time ABI assertions
    // that the editor surfaces as ordinary diagnostics while a source file is
    // edited. clangd exposes no stable out-of-tree plugin API, so instead of a
    // plugin this header turns "the struct changed but the contract did not"
    // into a static_assert failure at the point of inclusion. It is a separate
    // file: a translation unit includes it *after* the native declarations it
    // names, so it never affects consumers that only want the projection.
    {
        std::string check_path(output);
        const auto slash = check_path.find_last_of("/\\");
        check_path = slash == std::string::npos ? std::string("amc_abi_check.hpp")
                                                : check_path.substr(0, slash + 1) + "amc_abi_check.hpp";
        auto check = fmt::output_file(check_path);
        // Field width lookup for the per-field `sizeof` assertion below. AMC
        // already recorded the field's type size, so the check is derived from
        // the artifact rather than recomputed here.
        const auto field_type_size = [&](amc::Hash128 field_type) -> uint32_t {
            for (const auto &candidate : m.types)
                if (candidate.id == field_type) return candidate.size;
            return 0;
        };

        check.print(
            "#pragma once\n"
            AMC_GENERATED_FILE_WARNING("//")
            "// Generated by `amc generate -l cpp`.\n"
            "// clangd integration: ABI mismatches are diagnosed during editing and compilation.\n"
            "// Include this header after the native C++ declarations it references.\n"
            "//\n"
            "// ABI checks cover type size and alignment, field offsets, and field widths.\n"
            "// Bit-fields are skipped because `offsetof` and `sizeof` are not defined for them.\n"
            "// A mismatch indicates that the native layout may have drifted from the ABIX contract.\n"
            "#include \"amc_generated.hpp\"\n"
            "#include <stddef.h>\n\n");
        for (const auto &type : m.types) {
            if (!is_cpp_qualified_name(type.name)) continue;
            const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
            check.print(
                "static_assert(sizeof({0}) == ::amc_generated::{1}::size, "
                "\"ABIX ABI check: size mismatch for {0}\");\n",
                type.name, id);
            check.print(
                "static_assert(alignof({0}) == ::amc_generated::{1}::align, "
                "\"ABIX ABI check: align mismatch for {0}\");\n",
                type.name, id);
            // An exposed type is opaque and may hold private members, for which
            // `offsetof` is ill-formed.
            if ((type.flags & amc::type_opaque) != 0) continue;
            for (uint32_t i = 0; i < type.field_count; ++i) {
                const auto &field = m.fields[type.field_begin + i];
                if (!is_cpp_identifier(field.name)) continue;
                if ((field.flags & amc::field_bitfield) != 0) continue;
                check.print(
                    "static_assert(offsetof({0}, {2}) == ::amc_generated::{1}::{2}_offset, "
                    "\"ABIX ABI check: offset mismatch for {0}::{2}\");\n",
                    type.name, id, field.name);
                const uint32_t width = field_type_size(field.type_id);
                if (width == 0) continue;
                check.print(
                    "static_assert(sizeof(static_cast<{0} *>(nullptr)->{2}) == {3}, "
                    "\"ABIX ABI check: field width mismatch for {0}::{2}\");\n",
                    type.name, id, field.name, width);
            }
        }
    }
    return 0;
}

#if defined(__APPLE__)
// Derive the Homebrew LLVM installation prefix from the known resource-dir.
//   kClangResourceDir  →  /opt/homebrew/opt/llvm/lib/clang/22
//   brew_prefix        →  /opt/homebrew/opt/llvm
//
// Returns the empty string when the prefix cannot be derived.
// On non-macOS platforms this always returns empty.
static std::string brew_llvm_prefix() {
    if (!kClangResourceDir[0]) return {};
    llvm::SmallString<128> pfx(kClangResourceDir);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::sys::path::remove_filename(pfx);
    llvm::SmallString<128> sanity(pfx);
    llvm::sys::path::append(sanity, "include", "c++", "v1");
    return access(sanity.c_str(), F_OK) == 0 ? pfx.c_str() : std::string{};
}
#endif

#if defined(__APPLE__)
// Detect the macOS SDK path and append flags that mirror what the Clang
// driver would normally set when invoked from the command line.
//
// When loaded as a library (no config file is read), Clang does not
// automatically know its resource directory or the active SDK sysroot.
// This function fills those gaps.
//
// Include order (required by libc++ <stddef.h>):
//   1. libc++ headers  (Brew LLVM: -cxx-isystem via -nostdinc++)
//   2. Clang built-ins (via -resource-dir + -isystem)
//   3. System headers  (via -isysroot)
//
// The most important design rule is that **all three sets of headers must
// come from the same LLVM distribution**.  On macOS the default Xcode SDK
// ships its own libc++ at <sdk>/usr/include/c++/v1, but that version may
// be incompatible with the Homebrew LLVM's Clang built-in headers (e.g.
// std::string internal layout differs, leading to duplicate member errors).
//
// We therefore:
//   a. Derive the Homebrew LLVM prefix from the resource-dir known at
//      build time (kClangResourceDir);
//   b. Use -nostdinc++ to suppress the default C++ standard library search
//      (which would otherwise pick up the SDK's libc++);
//   c. Use -cxx-isystem to re-add only the Homebrew LLVM's own libc++.
//
// All three values are derived automatically: the resource directory is
// detected at build time via `clang -print-resource-dir` and injected through
// the ABIX_CLANG_RESOURCE_DIR compile definition; the SDK path is looked up
// at runtime from the SDKROOT environment variable or via xcrun.
void append_macos_sysroot(std::vector<std::string> &flags) {
    // ----- 1. Detect SDK path -------------------------------------------
    auto detect_sdk = []() -> std::string {
        if (const char *sdk = std::getenv("SDKROOT")) return sdk;
        FILE *fp = popen("xcrun --sdk macosx --show-sdk-path 2>/dev/null", "r");
        if (!fp) return {};
        char buf[4'096] = {0};
        std::string result;
        if (std::fgets(buf, sizeof(buf), fp)) result = buf;
        pclose(fp);
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
            result.pop_back();
        return result;
    };

    // ----- 2. Check for existing configuration ----------------------------
    // If the user (or the .abic.toml) already provides explicit sysroot or
    // a valid -isystem, don't override their choices.
    for (size_t i = 0; i < flags.size(); ++i) {
        const auto &f = flags[i];
        if (f == "-isysroot" || f.find("--sysroot") == 0) return;
        if (f == "-isystem" && i + 1 < flags.size()) {
            if (access(flags[i + 1].c_str(), F_OK) == 0) return;
        }
    }

    // ----- 3. Derive Homebrew LLVM prefix --------------------------------
    const std::string brew_prefix = brew_llvm_prefix();
    const bool have_brew_libcxx = !brew_prefix.empty();

    // ----- 4. macOS SDK setup --------------------------------------------
    //
    // Include order (all from the same LLVM distribution):
    //   1. libc++ headers   (Homebrew LLVM via -isystem)
    //   2. Clang built-ins  (<resource>/include via -isystem)
    //   3. System headers   (-isysroot)
    //
    // IMPORTANT: We use `-isystem` (NOT `-cxx-isystem`) for brew libc++.
    // In Clang's header search, `-isystem` paths are searched *before*
    // `-cxx-isystem` paths.  If we used `-cxx-isystem`, the resource-dir's
    // `-isystem` path would win over libc++, and `<stddef.h>` would fail
    // because it finds Clang's `<stddef.h>` instead of libc++'s wrapper.
    // ---------------------------------------------------------------------
    {
        std::string sdk = detect_sdk();
        if (!sdk.empty()) {
            // (a) libc++ – from Brew LLVM (compatible with our resource-dir)
            if (have_brew_libcxx) {
                llvm::SmallString<128> brew_libcxx(brew_prefix);
                llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
                flags.push_back("-nostdinc++");
                flags.push_back("-isystem");
                flags.push_back(brew_libcxx.c_str());
            }

            // (b) Clang built-in headers (stdarg.h, stddef.h, etc.)
            if (kClangResourceDir[0]) {
                flags.push_back("-resource-dir");
                flags.push_back(kClangResourceDir);
                llvm::SmallString<128> res_inc(kClangResourceDir);
                llvm::sys::path::append(res_inc, "include");
                if (access(res_inc.c_str(), F_OK) == 0) {
                    flags.push_back("-isystem");
                    flags.push_back(res_inc.c_str());
                }
            }

            // (c) SDK sysroot – system C headers, frameworks, etc.
            flags.push_back("-isysroot");
            flags.push_back(std::move(sdk));
        }
    }

    // ----- 5. Fallback (no SDK) ------------------------------------------
    // If no SDK is available, at least set the resource directory and
    // libc++ from the Brew LLVM prefix, so basic analysis still works.
    if (!flags.empty() && std::find(flags.begin(), flags.end(), "-resource-dir") == flags.end()) {
        // (a) libc++ from Brew LLVM (must use -isystem, not -cxx-isystem,
        //     because -cxx-isystem has lower priority than -isystem and
        //     would lose to the resource-dir's built-in headers below)
        if (have_brew_libcxx) {
            llvm::SmallString<128> brew_libcxx(brew_prefix);
            llvm::sys::path::append(brew_libcxx, "include", "c++", "v1");
            flags.push_back("-nostdinc++");
            flags.push_back("-isystem");
            flags.push_back(brew_libcxx.c_str());
        }

        // (b) Clang built-in headers
        if (kClangResourceDir[0]) {
            flags.push_back("-resource-dir");
            flags.push_back(kClangResourceDir);
            llvm::SmallString<128> res_inc(kClangResourceDir);
            llvm::sys::path::append(res_inc, "include");
            if (access(res_inc.c_str(), F_OK) == 0) {
                flags.push_back("-isystem");
                flags.push_back(res_inc.c_str());
            }
        }
    }
}
#endif

#if !defined(__APPLE__)
// Point the clang we are linked against at the resource directory detected at
// build time (`clang -print-resource-dir`, injected as ABIX_CLANG_RESOURCE_DIR).
// A `.abic.toml` used to spell this out by hand -- `-isystem
// /usr/lib/clang/22/include` -- which pins a toolchain version into a config
// file: it goes stale on the next upgrade, and on a case-insensitive filesystem
// it silently resolves a directory the source tree does not have. A config can
// still make its own choice by naming `-resource-dir` itself.
void append_clang_resource_dir(std::vector<std::string> &flags) {
    if (!kClangResourceDir[0]) return;
    for (const auto &f : flags) {
        if (f == "-resource-dir") return;
    }
    flags.push_back("-resource-dir");
    flags.push_back(kClangResourceDir);
}
#endif

}   // namespace amc::lang::cpp
