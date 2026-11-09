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

#include "SKL/AMC/Core/Reflect.h"

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
    return skl::amc::reflect::enum_name(static_cast<amc::PrimitiveABIKind>(kind & 0xffu), "unknown");
}

const char *float_format_name(uint32_t format) {
    return skl::amc::reflect::enum_name(
        static_cast<amc::FloatFormat>((format & amc::primitive_format_mask) >> amc::primitive_format_shift), "unknown");
}

const char *map_opcode_name(amc::MapOpcode opcode) { return skl::amc::reflect::enum_name(opcode, "unknown"); }

}   // namespace skl::amc::util
