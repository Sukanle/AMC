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
#include "SKL/AMC/Backend/Lua/Lua.h"
#include "SKL/AMC/Core/Generated.h"

#include <cctype>
#include <vector>

#include <fmt/format.h>

namespace skl::amc {
namespace {

std::string hash_literal(Hash128 value) { return fmt::format("0X{:016x}{:016x}", value.hi, value.lo); }

std::string cpp_string(const std::string &value) {
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
    return result + '"';
}

const Type *find_abi_type(const ABIModule &module, Hash128 id) {
    for (const auto &type : module.types)
        if (type.id == id) return &type;
    return nullptr;
}

const Type *find_abi_type(const ABIModule &module, const std::string &name) {
    for (const auto &type : module.types)
        if (type.name == name) return &type;
    return nullptr;
}

enum class EmittedFieldKind : uint32_t {
    INTEGER_SIGNED,
    INTEGER_UNSIGNED,
    FLOATING,
    BOOLEAN,
    POINTER,
    RECORD,
    ARRAY,
    OPAQUE,
    UNKNOWN,
};

EmittedFieldKind field_kind(const Type &type) {
    if (type.kind == TypeKind::POINTER) return EmittedFieldKind::POINTER;
    if (type.kind == TypeKind::ENUM)
        return (type.primitive_abi & primitive_signed_bit) != 0 ? EmittedFieldKind::INTEGER_SIGNED
                                                                : EmittedFieldKind::INTEGER_UNSIGNED;
    if (type.kind != TypeKind::PRIMITIVE) return EmittedFieldKind::UNKNOWN;

    switch (static_cast<PrimitiveABIKind>(type.primitive_abi & 0xffu)) {
        case PrimitiveABIKind::AMC_BOOL:           return EmittedFieldKind::BOOLEAN;
        case PrimitiveABIKind::AMC_FLOAT:          return EmittedFieldKind::FLOATING;
        case PrimitiveABIKind::AMC_SINT:
        case PrimitiveABIKind::AMC_SCHAR:
        case PrimitiveABIKind::AMC_CHAR_SIGNED:
        case PrimitiveABIKind::AMC_WCHAR_SIGNED:   return EmittedFieldKind::INTEGER_SIGNED;
        case PrimitiveABIKind::AMC_UINT:
        case PrimitiveABIKind::AMC_UCHAR:
        case PrimitiveABIKind::AMC_CHAR_UNSIGNED:
        case PrimitiveABIKind::AMC_CHAR8:
        case PrimitiveABIKind::AMC_CHAR16:
        case PrimitiveABIKind::AMC_CHAR32:
        case PrimitiveABIKind::AMC_WCHAR_UNSIGNED: return EmittedFieldKind::INTEGER_UNSIGNED;
        default:                                   break;
    }
    return EmittedFieldKind::UNKNOWN;
}

const char *field_kind_literal(EmittedFieldKind kind) {
    switch (kind) {
        case EmittedFieldKind::INTEGER_SIGNED:   return "skl::aue::FieldKind::INTEGER_SIGNED";
        case EmittedFieldKind::INTEGER_UNSIGNED: return "skl::aue::FieldKind::INTEGER_UNSIGNED";
        case EmittedFieldKind::FLOATING:         return "skl::aue::FieldKind::FLOATING";
        case EmittedFieldKind::BOOLEAN:          return "skl::aue::FieldKind::BOOLEAN";
        case EmittedFieldKind::POINTER:          return "skl::aue::FieldKind::POINTER";
        case EmittedFieldKind::RECORD:           return "skl::aue::FieldKind::RECORD";
        case EmittedFieldKind::ARRAY:            return "skl::aue::FieldKind::ARRAY";
        case EmittedFieldKind::OPAQUE:           return "skl::aue::FieldKind::OPAQUE";
        case EmittedFieldKind::UNKNOWN:          return "skl::aue::FieldKind::UNKNOWN";
    }
    return "skl::aue::FieldKind::UNKNOWN";
}

}   // namespace

std::string lua_function_symbol(std::string_view name) {
    std::string symbol;
    symbol.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            symbol += '_';
            ++i;
        } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            symbol += c;
        } else {
            symbol += '_';
        }
    }
    if (symbol.empty() || std::isdigit(static_cast<unsigned char>(symbol[0]))) symbol = "_" + symbol;
    return symbol;
}

std::string lua_entry_name(std::string_view package, std::string_view name) {
    if (!package.empty()) {
        const std::string prefix = std::string(package) + "_";
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
            return std::string(name.substr(prefix.size()));
    }
    return std::string(name);
}

std::string generate_lua_contract(const ABIModule &module, std::string &error) {
    error.clear();
    const std::string package = module.package_name.empty() ? "abix_module" : module.package_name;

    std::string out;
    out += AMC_GENERATED_FILE_WARNING("//");
    out += "// Generated by `amc generate -l lua`: Aue (Lua) boundary contract.\n";
    out += "// package: ";
    out += package;
    out += "\n";
    out += "// abi_hash = ";
    out += hash_literal(abi_hash(module));
    out += "\n";
    out += "#pragma once\n";
    out += "#include \"Aue/Aue.h\"\n";
    out += "\n";
    out += "namespace skl::amc::generated {\n";
    out += "\n";

    for (const auto &function : module.functions) {
        out += "extern \"C\" int ";
        out += lua_function_symbol(function.name);
        out += "(lua_State *);  // ";
        out += function.name;
        out += " signature=";
        out += hash_literal(function.signature);
        out += "\n";
    }
    out += "\n";

    const size_t count = module.functions.empty() ? 1 : module.functions.size();
    out += "inline const skl::aue::Entry lua_entries[";
    out += fmt::to_string(count);
    out += "] = {\n";
    for (const auto &function : module.functions) {
        out += "    {";
        out += cpp_string(lua_entry_name(package, function.name));
        out += ", ";
        out += fmt::to_string(static_cast<uint32_t>(function.params.size()));
        out += ", ";
        out += lua_function_symbol(function.name);
        out += "},\n";
    }
    if (module.functions.empty()) out += "    {nullptr, 0, nullptr},\n";
    out += "};\n\n";

    // Aue-2: expose record fields as descriptors so the runtime can read and
    // write native memory directly. Only records with fields are emitted; the
    // metatable is installed by register_contract_types().
    std::vector<size_t> record_types;
    for (size_t i = 0; i < module.types.size(); ++i) {
        const Type &type = module.types[i];
        if (type.kind != TypeKind::RECORD) continue;
        for (uint32_t j = 0; j < type.field_count; ++j) {
            const Field &field = module.fields[type.field_begin + j];
            if (field.owner_type == Hash128{} || field.owner_type == type.id) {
                record_types.push_back(i);
                break;
            }
        }
    }

    std::vector<uint32_t> record_index(module.types.size(), 0xFFFFFFFFu);
    for (size_t emitted = 0; emitted < record_types.size(); ++emitted)
        record_index[record_types[emitted]] = static_cast<uint32_t>(emitted);

    for (size_t emitted = 0; emitted < record_types.size(); ++emitted) {
        const Type &type = module.types[record_types[emitted]];
        std::vector<const Field *> fields;
        for (uint32_t i = 0; i < type.field_count; ++i) {
            const Field &field = module.fields[type.field_begin + i];
            if (!(field.owner_type == Hash128{}) && !(field.owner_type == type.id)) continue;
            fields.push_back(&field);
        }
        out += "inline const skl::aue::Field lua_type_";
        out += fmt::to_string(emitted);
        out += "_fields[] = {\n";
        for (const Field *field_ptr : fields) {
            const Field &field = *field_ptr;
            const Type *field_type = find_abi_type(module, field.type_id);

            EmittedFieldKind kind = EmittedFieldKind::UNKNOWN;
            uint32_t type_index = 0xFFFFFFFFu;
            uint32_t count = 0;
            uint32_t stride = 0;
            EmittedFieldKind element_kind = EmittedFieldKind::UNKNOWN;
            uint32_t element_size = 0;

            if (field_type != nullptr) {
                if (field_type->kind == TypeKind::RECORD && field_type->field_count > 0) {
                    kind = EmittedFieldKind::RECORD;
                    const auto found = find_abi_type(module, field_type->name);
                    if (found != nullptr) type_index = record_index[static_cast<size_t>(found - module.types.data())];
                } else if (field_type->kind == TypeKind::ARRAY) {
                    kind = EmittedFieldKind::ARRAY;
                    count = field_type->array_count;
                    stride = count != 0 ? field_type->size / count : 0;
                    const size_t bracket = field_type->name.find('[');
                    const std::string base =
                        bracket == std::string::npos ? std::string{} : field_type->name.substr(0, bracket);
                    const Type *element = base.empty() ? nullptr : find_abi_type(module, base);
                    if (element != nullptr) {
                        element_size = element->size;
                        if (element->kind == TypeKind::RECORD && element->field_count > 0) {
                            element_kind = EmittedFieldKind::RECORD;
                            const auto found = find_abi_type(module, element->name);
                            if (found != nullptr)
                                type_index = record_index[static_cast<size_t>(found - module.types.data())];
                        } else {
                            element_kind = field_kind(*element);
                        }
                    } else {
                        element_size = stride;
                    }
                } else {
                    kind = field_kind(*field_type);
                }
            }
            if (kind == EmittedFieldKind::UNKNOWN) kind = EmittedFieldKind::OPAQUE;

            out += "    {";
            out += cpp_string(field.name);
            out += ", ";
            out += fmt::to_string(field.offset);
            out += ", ";
            out += fmt::to_string(field_type == nullptr ? 0 : field_type->size);
            out += ", ";
            out += field_kind_literal(kind);
            out += ", ";
            out += fmt::to_string(type_index);
            out += "u, ";
            out += fmt::to_string(count);
            out += ", ";
            out += fmt::to_string(stride);
            out += ", ";
            out += field_kind_literal(element_kind);
            out += ", ";
            out += fmt::to_string(element_size);
            out += "},\n";
        }
        out += "};\n\n";
    }

    if (!record_types.empty()) {
        out += "inline const skl::aue::Type lua_types[] = {\n";
        for (size_t emitted = 0; emitted < record_types.size(); ++emitted) {
            const Type &type = module.types[record_types[emitted]];
            size_t owned_fields = 0;
            for (uint32_t i = 0; i < type.field_count; ++i) {
                const Field &field = module.fields[type.field_begin + i];
                if (field.owner_type == Hash128{} || field.owner_type == type.id) ++owned_fields;
            }
            out += "    {";
            out += cpp_string(type.name);
            out += ", ";
            out += fmt::to_string(type.size);
            out += ", ";
            out += fmt::to_string(type.align);
            out += ", lua_type_";
            out += fmt::to_string(emitted);
            out += "_fields, ";
            out += fmt::to_string(owned_fields);
            out += "},\n";
        }
        out += "};\n\n";
    }

    out += "inline const skl::aue::Contract lua_contract = {\n";
    out += "    ";
    out += cpp_string(package);
    out += ",\n";
    out += "    ";
    out += cpp_string(hash_literal(abi_hash(module)));
    out += ",\n";
    out += "    lua_entries,\n";
    out += "    ";
    out += fmt::to_string(module.functions.size());
    out += ",\n";
    if (record_types.empty()) {
        out += "    nullptr,\n";
        out += "    0,\n";
    } else {
        out += "    lua_types,\n";
        out += "    ";
        out += fmt::to_string(record_types.size());
        out += ",\n";
    }
    out += "};\n";
    out += "\n";
    out += "}  // namespace skl::amc::generated\n";
    return out;
}

std::string generate_lua_conformance(const ABIModule &module, std::string &error) {
    error.clear();
    const std::string package = module.package_name.empty() ? "abix_module" : module.package_name;
    const std::string hash = hash_literal(abi_hash(module));

    std::string out;
    out += AMC_GENERATED_FILE_WARNING("--");
    out += "-- Generated by `amc generate -l lua`: Aue conformance script.\n";
    out += "-- package: ";
    out += package;
    out += "\n";
    out += "-- Compile-time metadata drives this test; run it under L0 and L1 and\n";
    out += "-- the observable output must be identical.\n";
    out += "-- abi_hash = ";
    out += hash;
    out += "\n";
    out += "local expected = {\n";
    for (const auto &function : module.functions) {
        out += "    ";
        out += cpp_string(lua_entry_name(package, function.name));
        out += ",\n";
    }
    out += "}\n";
    out += "local seen = 0\n";
    out += "for _, name in ipairs(expected) do\n";
    out += "    assert(type(counter[name]) == \"function\", \"missing binding: \" .. name)\n";
    out += "    seen = seen + 1\n";
    out += "end\n";
    out += "assert(counter._module == ";
    out += cpp_string(package);
    out += ", \"module mismatch\")\n";
    out += "assert(counter._abi_hash == ";
    out += cpp_string(hash);
    out += ", \"abi hash mismatch\")\n";
    out += "print(\"aue-conformance\", seen, counter._module)\n";
    return out;
}

}   // namespace skl::amc
