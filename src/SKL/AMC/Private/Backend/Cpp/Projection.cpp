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
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/Generated.h"
#include "SKL/AMC/Lang/Lang.h"

#include <cstdint>
#include <string>
#include <vector>

namespace amc::lang::backend::cpp {

void Emit::types() {
    for (const auto &type : _m.types) {
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        _o.print(
            "struct {0} {{\n"
            "    static constexpr TypeId type_id {{{1}, {2}}};\n"
            "    static constexpr uint64_t type_id_lo = {1};\n"
            "    static constexpr uint64_t type_id_hi = {2};\n"
            "    static constexpr LayoutInfo layout {{{3}, {4}, {5}}};\n"
            "    static constexpr size_t size = {3};\n"
            "    static constexpr size_t align = {4};\n",
            id, hex_u64(type.id.lo), hex_u64(type.id.hi), type.size, type.align, type.field_count);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = _m.fields[type.field_begin + i];
            _o.print("    static constexpr size_t {}_offset = {};\n", cpp_name(field.name), field.offset);
        }
        _o.print(
            "}};\n"
            "template <> struct TypeTraits<{0}> {{\n"
            "    static constexpr TypeId type_id {{{1}, {2}}};\n"
            "    static constexpr LayoutInfo layout {{{3}, {4}, {5}}};\n"
            "    static constexpr size_t size = {3};\n"
            "    static constexpr size_t align = {4};\n"
            "}};\n"
            "inline constexpr FieldInfo {0}_fields[{6}] = {{\n",
            id, hex_u64(type.id.lo), hex_u64(type.id.hi), type.size, type.align, type.field_count,
            type.field_count == 0 ? 1 : type.field_count);
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const auto &field = _m.fields[type.field_begin + i];
            _o.print("    {{{{{}, {}}}, {}, {}}},\n", hex_u64(field.type_id.lo), hex_u64(field.type_id.hi),
                field.offset, field.flags);
        }
        _o.print("}};\n");
    }
}

void Emit::functions() {
    for (size_t i = 0; i < _m.functions.size(); ++i) {
        const auto &function = _m.functions[i];
        const auto param_count = function.parameters.empty() ? 1 : function.parameters.size();
        _o.print("inline constexpr ParameterInfo amc_function_{}_parameters[{}] = {{\n", i, param_count);
        for (const auto &parameter : function.parameters)
            _o.print("    {{{{{}, {}}}, {}}},\n", hex_u64(parameter.type_id.lo), hex_u64(parameter.type_id.hi),
                parameter.flags);
        _o.print("}};\n");
        _o.print(
            "inline constexpr FunctionInfo amc_function_{} {{amc_function_{}_parameters, {{{}, {}}},"
            " {{{}, {}}}, {}, {}, {}}};\n",
            i, i, hex_u64(function.signature.lo), hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
            hex_u64(function.return_type.hi), function.parameters.size(), function.calling_convention, function.flags);
    }
}

void Emit::tables() {
    {
        const auto type_count = _m.types.empty() ? 1 : _m.types.size();
        _o.print("inline constexpr ::skl::abix::metadata::TypeDescriptor amc_types[{}] = {{\n", type_count);
        for (const auto &type : _m.types) {
            const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
            _o.print("    {{{}_fields, {{{}, {}}}, {{{}, {}}}, {}, {}, {}, {}}},\n", id, hex_u64(type.id.lo),
                hex_u64(type.id.hi), hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi), type.flags, type.size,
                type.align, type.field_count);
        }
        _o.print("}};\n");
    }

    // Canonical TypeDesc / TypeLayout arrays — the one ABI truth.
    // These are imported directly by RuntimeRegistry instead of being
    // reconstructed from the runtime projection (amc_types[]).
    {
        const auto type_count = _m.types.empty() ? 1 : _m.types.size();
        _o.print("inline constexpr ::skl::abix::model::TypeDesc amc_canonical_types[{}] = {{\n", type_count);
        for (size_t i = 0; i < _m.types.size(); ++i) {
            const auto &type = _m.types[i];
            // name_offset = 0 (names live in .abix.names section for
            // diagnostic use only — stripped from the runtime registry).
            _o.print("    {{{{{}, {}}}, {}, {}, {}}},\n", hex_u64(type.id.lo), hex_u64(type.id.hi), type.flags, 0U,
                static_cast<uint32_t>(i));
        }
        if (_m.types.empty()) {
            _o.print("    {{{{{}, {}}}, {}, {}, {}}},\n", "0ULL", "0ULL", 0U, 0U, 0U);
        }
        _o.print("}};\n");

        _o.print("inline constexpr ::skl::abix::model::TypeLayout amc_canonical_layouts[{}] = {{\n", type_count);
        for (const auto &type : _m.types) {
            _o.print("    {{{}, {}, {}, {}, {{{}, {}}}}},\n", type.size, type.align, type.field_begin, type.field_count,
                hex_u64(type.layout_hash.lo), hex_u64(type.layout_hash.hi));
        }
        if (_m.types.empty()) {
            _o.print("    {{{}, {}, {}, {}, {{{}, {}}}}},\n", 0U, 0U, 0U, 0U, "0ULL", "0ULL");
        }
        _o.print("}};\n");
    }

    {
        const auto func_count = _m.functions.empty() ? 1 : _m.functions.size();
        _o.print("inline constexpr ::skl::abix::metadata::FunctionDescriptor amc_functions[{}] = {{\n", func_count);
        for (size_t i = 0; i < _m.functions.size(); ++i) {
            const auto &function = _m.functions[i];
            _o.print("    {{amc_function_{}_parameters, {{{}, {}}}, {{{}, {}}}, {}, {}, {}}},\n", i,
                hex_u64(function.signature.lo), hex_u64(function.signature.hi), hex_u64(function.return_type.lo),
                hex_u64(function.return_type.hi), function.parameters.size(), function.calling_convention,
                function.flags);
        }
        _o.print("}};\n");
    }

    {
        const auto sym_count = _m.symbols.empty() ? 1 : _m.symbols.size();
        _o.print("inline constexpr SymbolInfo amc_symbols[{}] = {{\n", sym_count);
        for (const auto &symbol : _m.symbols)
            _o.print("    {{{}, {}}},\n", static_cast<uint32_t>(symbol.kind), symbol.target_index);
        _o.print("}};\n");
    }

    // Struct layout: name, version, types, canonical_types, canonical_layouts,
    // type_count, function_count, functions, symbols, symbol_count.
    _o.print(
        "inline constexpr ModuleInfo amc_module{{{}, {}, amc_types, amc_canonical_types, amc_canonical_layouts, {}, "
        "{}, amc_functions, amc_symbols, {}}};\n",
        cpp_string(_m.package_name), cpp_string(_m.package_version), _m.types.size(), _m.functions.size(),
        _m.symbols.size());
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
void Emit::names_section() {
    if (_m.types.empty()) return;
    _o.print(
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
    for (const auto &type : _m.types)
        _o.print("    {},\n", cpp_string(type.name));
    _o.print(
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
int Emit::metadata_region(std::string &error) {
    amc::MetadataOptions metadata_options;
    std::vector<uint8_t> region;
    if (!amc::build_metadata_region(_m, metadata_options, region, error)) return 1;
    const uint32_t region_size_value = static_cast<uint32_t>(region.size());
    _o.print(
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
        if (i % 12 == 0) _o.print("    ");
        _o.print("0x{:02x},", region[i]);
        if (i % 12 == 11 || i + 1 == region.size()) _o.print("\n");
    }
    _o.print(
        "}};\n"
        "#if defined(_WIN32) && defined(_MSC_VER)\n"
        "// selectany + /INCLUDE keep the section past /OPT:REF despite being unreferenced.\n"
        "#  pragma comment(linker, \"/INCLUDE:amc_metadata_region\")\n"
        "#endif\n");
    _o.print("inline constexpr size_t amc_metadata_region_size = sizeof(amc_metadata_region);\n");
    return 0;
}

void Emit::maps() {
    for (size_t i = 0; i < _m.maps.size(); ++i) {
        const auto &map = _m.maps[i];
        const auto op_count = map.operations.empty() ? 1 : map.operations.size();
        _o.print("inline constexpr MapOperation amc_map_{}_operations[{}] = {{\n", i, op_count);
        for (const auto &operation : map.operations) {
            const char *opname = operation.opcode == amc::MapOpcode::copy_field    ? "copy_field"
                               : operation.opcode == amc::MapOpcode::convert_int   ? "convert_int"
                               : operation.opcode == amc::MapOpcode::convert_float ? "convert_float"
                               : operation.opcode == amc::MapOpcode::add_default   ? "add_default"
                                                                                   : "skip_field";
            _o.print("    {{MapOpcode::{}, {}, {}, {}, {}, {}, {{{}, {}}}}},\n", opname, operation.source_field,
                operation.target_field, operation.source_offset, operation.target_offset, operation.byte_count,
                hex_u64(operation.auxiliary.lo), hex_u64(operation.auxiliary.hi));
        }
        _o.print(
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
                    _o.print(
                        "    memcpy(static_cast<char *>(target) + {}, static_cast<const char *>(source) + {}, "
                        "{});\n",
                        operation.target_offset, operation.source_offset, operation.byte_count);
                    break;
                case amc::MapOpcode::add_default:
                    _o.print("    memset(static_cast<char *>(target) + {}, 0, {});\n", operation.target_offset,
                        operation.byte_count);
                    break;
                case amc::MapOpcode::skip_field:  break;
                case amc::MapOpcode::convert_int:
                case amc::MapOpcode::convert_float:
                    _o.print("    return false;  // conversion requires an explicit native converter\n");
                    break;
            }
        }
        _o.print(
            "    return true;\n"
            "}}\n"
            "}};\n");
    }
}


int Emit::projection(std::string &error) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    types();
    functions();
    tables();
    names_section();
    if (const int status = metadata_region(error); status != 0) return status;
    maps();
    return 0;
}

void Emit::native_type_traits() {
    _o.print(
        "}}  // namespace amc_generated\n"
        "#ifdef AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n"
        "namespace skl::abix::runtime {{\n");
    for (const auto &type : _m.types) {
        if (type.kind != amc::TypeKind::record
            || (type.flags & amc::type_template_primary) != 0
            || type.name.find('<') != std::string::npos
            || type.name.rfind("std::", 0) == 0)
            continue;
        const std::string id = fmt::format("{}_ABIX", cpp_name(type.name));
        _o.print(
            "template <> struct TypeTraits<::{}> {{\n"
            "  static constexpr ::skl::abix::model::TypeId type_id = ::amc_generated::{}::type_id;\n"
            "}};\n",
            type.name, id);
    }
    _o.print(
        "}}  // namespace skl::abix::runtime\n"
        "#endif  // AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS\n");
}

}   // namespace amc::lang::backend::cpp
