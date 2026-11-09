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
#include "SKL/AMC/Private/Frontend/Cpp/Native.h"

#include <llvm/ADT/APFloat.h>

#include <cctype>
#include <string>

namespace skl::amc::lang::frontend::cpp {
namespace {

// Stable, language-independent name for a floating-point format. Two float
// types only share an identity when they share an actual IEEE/format semantic
// (e.g. x87 80-bit must never merge with binary128, and a target where
// `long double` is just `double` must merge with it).
const char *float_semantics_tag(const llvm::fltSemantics &semantics) {
    using llvm::APFloatBase;
    if (&semantics == &APFloatBase::IEEEhalf()) return "ieee16";
    if (&semantics == &APFloatBase::BFloat()) return "bf16";
    if (&semantics == &APFloatBase::IEEEsingle()) return "ieee32";
    if (&semantics == &APFloatBase::IEEEdouble()) return "ieee64";
    if (&semantics == &APFloatBase::IEEEquad()) return "ieee128";
    if (&semantics == &APFloatBase::x87DoubleExtended()) return "x87_80";
    if (&semantics == &APFloatBase::PPCDoubleDouble()) return "ppc_double_double";
    return "unknown";
}

amc::FloatFormat float_format_code(const llvm::fltSemantics &semantics) {
    using llvm::APFloatBase;
    if (&semantics == &APFloatBase::IEEEhalf()) return amc::FloatFormat::IEEE16;
    if (&semantics == &APFloatBase::BFloat()) return amc::FloatFormat::BF16;
    if (&semantics == &APFloatBase::IEEEsingle()) return amc::FloatFormat::IEEE32;
    if (&semantics == &APFloatBase::IEEEdouble()) return amc::FloatFormat::IEEE64;
    if (&semantics == &APFloatBase::IEEEquad()) return amc::FloatFormat::IEEE128;
    if (&semantics == &APFloatBase::x87DoubleExtended()) return amc::FloatFormat::X87_80;
    if (&semantics == &APFloatBase::PPCDoubleDouble()) return amc::FloatFormat::PPC_DOUBLE_DOUBLE;
    return amc::FloatFormat::NONE;
}

// Pick the ABI kind that matches the observed signedness of a type whose C
// spelling does not fix it (`char`, `wchar_t`).
amc::PrimitiveABIKind signedness_kind(
    bool is_signed, amc::PrimitiveABIKind signed_kind, amc::PrimitiveABIKind unsigned_kind) {
    return is_signed ? signed_kind : unsigned_kind;
}

}   // namespace

std::string primitive_abi_key(clang::ASTContext &ctx, clang::QualType qt, uint32_t &abi) {
    const auto *bt = qt->getAs<clang::BuiltinType>();
    if (bt == nullptr) return {};
    const uint32_t bytes = uint32_t(ctx.getTypeSize(qt) / 8);
    const bool is_signed = bt->isSignedInteger();
    switch (bt->getKind()) {
        case clang::BuiltinType::Void:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_VOID);
            return "prim:void";
        case clang::BuiltinType::Bool:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_BOOL);
            return "prim:bool";
        case clang::BuiltinType::Char_S:
        case clang::BuiltinType::Char_U:
            // Plain `char`: signedness is target-defined, so the kind encodes
            // the *actual* signedness, not the spelling.
            abi = amc::primitive_abi(bytes, signedness_kind(is_signed, amc::PrimitiveABIKind::AMC_CHAR_SIGNED,
                                                amc::PrimitiveABIKind::AMC_CHAR_UNSIGNED));
            return is_signed ? "prim:char_s" : "prim:char_u";
        case clang::BuiltinType::SChar:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_SCHAR, amc::FloatFormat::NONE, true);
            return "prim:schar";
        case clang::BuiltinType::UChar:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_UCHAR);
            return "prim:uchar";
        case clang::BuiltinType::Char8:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_CHAR8);
            return "prim:char8";
        case clang::BuiltinType::Char16:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_CHAR16);
            return "prim:char16";
        case clang::BuiltinType::Char32:
            abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_CHAR32);
            return "prim:char32";
        case clang::BuiltinType::WChar_S:
        case clang::BuiltinType::WChar_U:
            abi = amc::primitive_abi(bytes, signedness_kind(is_signed, amc::PrimitiveABIKind::AMC_WCHAR_SIGNED,
                                                amc::PrimitiveABIKind::AMC_WCHAR_UNSIGNED));
            return std::string("prim:wchar") + (is_signed ? "_s" : "_u");
        default: break;
    }
    if (bt->isInteger()) {
        // Integer width *is* ABI: `int` and `long` differ, so the key carries
        // width and signedness. `long`/`long long` still collapse because they
        // share both. The width is in bits (`prim:i32`), matching the `i8`/`u64`
        // spelling the rest of the toolchain uses.
        abi = amc::primitive_abi(bytes, is_signed ? amc::PrimitiveABIKind::AMC_SINT : amc::PrimitiveABIKind::AMC_UINT,
            amc::FloatFormat::NONE, is_signed);
        return std::string(is_signed ? "prim:i" : "prim:u") + std::to_string(bytes * 8);
    }
    if (bt->isFloatingPoint()) {
        // Floats are identified by format, not spelling: a target where
        // `double` is 32-bit IEEE collapses it with `float` (same ABI), while
        // x87 80-bit and IEEE binary128 stay distinct despite equal size.
        const auto format = float_format_code(ctx.getFloatTypeSemantics(qt));
        abi = amc::primitive_abi(bytes, amc::PrimitiveABIKind::AMC_FLOAT, format);
        return std::string("prim:f") + float_semantics_tag(ctx.getFloatTypeSemantics(qt));
    }
    return {};
}

NativeKind native_kind_of(const std::string &name) {
    if (name == "std::string") return NativeKind::STRING;
    if (name == "std::string_view") return NativeKind::STRING_VIEW;
    if (name == "std::vector") return NativeKind::VECTOR;
    if (name == "std::span") return NativeKind::SPAN;
    return NativeKind::NONE;
}

std::string normalise_element(const std::string &spelling) {
    std::string out;
    std::string token;
    const auto flush = [&] {
        if (token.empty()) return;
        if (token != "const" && token != "class" && token != "struct" && token != "volatile" && token != "typename") {
            if (!out.empty()) out += ' ';
            out += token;
        }
        token.clear();
    };
    for (const char c : spelling) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '&') {
            flush();
            continue;
        }
        token += c;
    }
    flush();
    return out;
}

std::string native_type_name(NativeKind kind, const std::string &element) {
    switch (kind) {
        case NativeKind::STRING:      return "std::string";
        case NativeKind::STRING_VIEW: return "std::string_view";
        case NativeKind::VECTOR:      return "std::vector<" + element + ">";
        case NativeKind::SPAN:        return "std::span<" + element + ">";
        default:                      return {};
    }
}

bool fixed_width_int(const std::string &spelling, uint32_t &size, bool &is_signed) {
    struct Entry {
        const char *name;
        uint32_t size;
        bool is_signed;
    };
    static constexpr Entry kEntries[] = {
        {  "int8_t", 1,  true},
        { "uint8_t", 1, false},
        { "int16_t", 2,  true},
        {"uint16_t", 2, false},
        { "int32_t", 4,  true},
        {"uint32_t", 4, false},
        { "int64_t", 8,  true},
        {"uint64_t", 8, false},
    };
    const std::string name = normalise_element(spelling);
    for (const auto &entry : kEntries)
        if (name == entry.name) {
            size = entry.size;
            is_signed = entry.is_signed;
            return true;
        }
    return false;
}

}   // namespace skl::amc::lang::frontend::cpp
