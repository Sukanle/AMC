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
#include "SKL/AMC/Util/Names.h"

namespace skl::amc::util {

std::string arch_name(uint32_t value) {
    switch (value) {
        case 0:  return "x86_64";
        case 1:  return "aarch64";
        case 2:  return "riscv64";
        default: return "unknown";
    }
}

std::string os_name(uint32_t value) {
    switch (value) {
        case 0:  return "linux";
        case 1:  return "windows";
        case 2:  return "macos";
        default: return "unknown";
    }
}

std::string compiler_name(uint32_t value) {
    switch (value) {
        case 0:  return "gcc";
        case 1:  return "clang";
        case 2:  return "msvc";
        default: return "unknown";
    }
}

std::string target_calling_convention_name(uint32_t value) {
    switch (value) {
        case 0:  return "sysv_abi";
        case 1:  return "ms_abi";
        case 2:  return "aapcs";
        default: return "unknown";
    }
}

std::string function_calling_convention_name(uint32_t value) {
    switch (value) {
        case 0:  return "unspecified";
        case 1:  return "c";
        case 2:  return "stdcall";
        case 3:  return "fastcall";
        case 4:  return "thiscall";
        case 5:  return "aarch64_sve";
        default: return "unknown";
    }
}

const char *primitive_kind_name(uint32_t kind) {
    switch (static_cast<amc::PrimitiveABIKind>(kind & 0xffu)) {
        case amc::PrimitiveABIKind::AMC_NONE:           return "none";
        case amc::PrimitiveABIKind::AMC_VOID:           return "void";
        case amc::PrimitiveABIKind::AMC_BOOL:           return "bool";
        case amc::PrimitiveABIKind::AMC_CHAR_SIGNED:    return "char_signed";
        case amc::PrimitiveABIKind::AMC_CHAR_UNSIGNED:  return "char_unsigned";
        case amc::PrimitiveABIKind::AMC_SCHAR:          return "schar";
        case amc::PrimitiveABIKind::AMC_UCHAR:          return "uchar";
        case amc::PrimitiveABIKind::AMC_CHAR8:          return "char8";
        case amc::PrimitiveABIKind::AMC_CHAR16:         return "char16";
        case amc::PrimitiveABIKind::AMC_CHAR32:         return "char32";
        case amc::PrimitiveABIKind::AMC_WCHAR_SIGNED:   return "wchar_signed";
        case amc::PrimitiveABIKind::AMC_WCHAR_UNSIGNED: return "wchar_unsigned";
        case amc::PrimitiveABIKind::AMC_SINT:           return "sint";
        case amc::PrimitiveABIKind::AMC_UINT:           return "uint";
        case amc::PrimitiveABIKind::AMC_FLOAT:          return "floating";
    }
    return "unknown";
}

const char *float_format_name(uint32_t format) {
    switch (static_cast<amc::FloatFormat>((format & amc::primitive_format_mask) >> amc::primitive_format_shift)) {
        case amc::FloatFormat::NONE:              return "none";
        case amc::FloatFormat::IEEE16:            return "ieee16";
        case amc::FloatFormat::BF16:              return "bf16";
        case amc::FloatFormat::IEEE32:            return "ieee32";
        case amc::FloatFormat::IEEE64:            return "ieee64";
        case amc::FloatFormat::IEEE128:           return "ieee128";
        case amc::FloatFormat::X87_80:            return "x87_80";
        case amc::FloatFormat::PPC_DOUBLE_DOUBLE: return "ppc_double_double";
    }
    return "unknown";
}

const char *map_opcode_name(amc::MapOpcode opcode) {
    switch (opcode) {
        case amc::MapOpcode::COPY_FIELD:    return "copy_field";
        case amc::MapOpcode::CONVERT_INT:   return "convert_int";
        case amc::MapOpcode::CONVERT_FLOAT: return "convert_float";
        case amc::MapOpcode::ADD_DEFAULT:   return "add_default";
        case amc::MapOpcode::SKIP_FIELD:    return "skip_field";
    }
    return "unknown";
}

}   // namespace skl::amc::util
