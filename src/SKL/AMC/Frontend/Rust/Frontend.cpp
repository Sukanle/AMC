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
#include "SKL/AMC/Frontend/Rust/Rust.h"

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Semantic/Identity.h"
#include "SKL/AMC/Semantic/Serialize.h"
#include "SKL/AMC/Semantic/Sync.h"
#include "SKL/AMC/Support/Json.h"

#include <algorithm>
#include <cctype>
#include <stdint.h>
#include <stdlib.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

// Source-level Rust extractor.
//
// This is intentionally a *preliminary* frontend: it understands the subset of
// Rust that crosses an FFI boundary — `#[repr(C)]` structs/enums and
// `extern "C"` functions — and computes the C layout itself (the same layout
// rules the C++ frontend obtains from Clang). Generics, traits, lifetimes and
// bit-fields are out of scope; unsupported constructs are ignored rather than
// guessed at, per `LANGUAGE-PLUGIN.md`.
namespace amc::lang::rust {
namespace {

constexpr uint64_t kTypeDomain = 0x54595045;   // "TYPE"

// Target facts the Rust layout fallback needs. Probe results are authoritative;
// these values are the minimum needed when the parser computes a layout without
// running rustc (Host == Target or cross-target after facts are known).
struct TargetFacts {
    uint32_t arch = 0;
    uint32_t os = 0;
    uint32_t target_abi = 0;
    uint32_t pointer_bits = 0;
    uint32_t endianness = 0;   // 0 unknown, 1 little, 2 big
    uint32_t pointer_size = 0;
    uint32_t c_long_size = 0;
};

struct HelperField {
    std::string name;
    std::string type;
    uint64_t offset = 0;
};

struct HelperType {
    std::string id;
    std::string name;
    std::string path;
    std::string kind;
    std::string underlying;
    std::vector<std::string> repr;
    uint64_t size = 0;
    uint32_t align = 1;
    bool generic = false;
    std::vector<HelperField> fields;
};

struct HelperFunction {
    std::string name;
    std::string path;
    std::string symbol;
    std::string abi;
    std::string visibility;
    std::string kind;
    std::string owner;
    std::string return_type;
    bool has_receiver = false;
    std::vector<HelperField> parameters;
};

// Facts imported from `amc.rust.semantic/1`. The helper is the semantic
// authority; the parser is the fallback when the helper is unavailable.
struct HelperDocument {
    bool loaded = false;
    std::vector<HelperType> types;
    std::vector<HelperFunction> functions;
};

uint64_t json_uint(const amc::Json *value, uint64_t fallback = 0) {
    return value == nullptr ? fallback : static_cast<uint64_t>(value->as_number(static_cast<double>(fallback)));
}

void parse_helper_field(const amc::Json &value, HelperField &field) {
    if (const amc::Json *name = value.find("name")) field.name = name->as_string();
    if (const amc::Json *type = value.find("type")) field.type = type->as_string();
    field.offset = json_uint(value.find("offset"));
}

void parse_helper_document(const amc::Json &root, HelperDocument &document) {
    const amc::Json *types = root.find("types");
    if (types != nullptr && types->is_array()) {
        for (const amc::Json &entry : types->items()) {
            if (!entry.is_object()) continue;
            HelperType type;
            if (const amc::Json *value = entry.find("id")) type.id = value->as_string();
            if (const amc::Json *value = entry.find("name")) type.name = value->as_string();
            if (const amc::Json *value = entry.find("path")) type.path = value->as_string();
            if (const amc::Json *value = entry.find("kind")) type.kind = value->as_string();
            if (const amc::Json *value = entry.find("underlying")) type.underlying = value->as_string();
            if (const amc::Json *value = entry.find("size")) type.size = json_uint(value);
            if (const amc::Json *value = entry.find("align")) type.align = static_cast<uint32_t>(json_uint(value, 1));
            if (const amc::Json *value = entry.find("generic")) type.generic = value->as_bool();
            if (const amc::Json *value = entry.find("repr"); value != nullptr && value->is_array())
                for (const amc::Json &item : value->items())
                    type.repr.push_back(item.as_string());
            if (const amc::Json *value = entry.find("fields"); value != nullptr && value->is_array())
                for (const amc::Json &item : value->items()) {
                    HelperField field;
                    parse_helper_field(item, field);
                    type.fields.push_back(std::move(field));
                }
            if (!type.id.empty()) document.types.push_back(std::move(type));
        }
    }

    const amc::Json *functions = root.find("functions");
    if (functions != nullptr && functions->is_array()) {
        for (const amc::Json &entry : functions->items()) {
            if (!entry.is_object()) continue;
            HelperFunction function;
            if (const amc::Json *value = entry.find("name")) function.name = value->as_string();
            if (const amc::Json *value = entry.find("path")) function.path = value->as_string();
            if (const amc::Json *value = entry.find("symbol")) function.symbol = value->as_string();
            if (const amc::Json *value = entry.find("abi")) function.abi = value->as_string();
            if (const amc::Json *value = entry.find("visibility")) function.visibility = value->as_string();
            if (const amc::Json *value = entry.find("kind")) function.kind = value->as_string();
            if (const amc::Json *value = entry.find("owner")) function.owner = value->as_string();
            if (const amc::Json *value = entry.find("return_type")) function.return_type = value->as_string();
            if (const amc::Json *value = entry.find("has_receiver")) function.has_receiver = value->as_bool();
            if (const amc::Json *value = entry.find("parameters"); value != nullptr && value->is_array())
                for (const amc::Json &item : value->items()) {
                    HelperField field;
                    parse_helper_field(item, field);
                    function.parameters.push_back(std::move(field));
                }
            if (!function.name.empty()) document.functions.push_back(std::move(function));
        }
    }
    document.loaded = true;
}

bool target_facts_from(const amc::lang::Config &config, TargetFacts &target, std::string &error) {
    if (config.pointer_bits == 0) {
        error = "Rust frontend requires [target] pointer_bits";
        return false;
    }
    if (config.pointer_bits != 16 && config.pointer_bits != 32 && config.pointer_bits != 64) {
        error = "Rust frontend only supports 16/32/64-bit pointers";
        return false;
    }
    target.arch = config.arch;
    target.os = config.os;
    target.target_abi = config.target_abi;
    target.pointer_bits = config.pointer_bits;
    target.endianness = config.endianness;
    target.pointer_size = config.pointer_bits / 8;
    // ABIX target facts use os=1 for Windows. Windows x64 is LLP64, so c_long
    // is 32-bit even when pointers are 64-bit; other supported targets follow
    // the pointer width. An explicit c_long_bits always wins.
    const uint32_t default_c_long_bits = config.os == 1 ? 32 : config.pointer_bits;
    target.c_long_size = (config.c_long_bits != 0 ? config.c_long_bits : default_c_long_bits) / 8;
    if (target.c_long_size != 4 && target.c_long_size != 8) {
        error = "Rust frontend only supports 32/64-bit c_long";
        return false;
    }
    return true;
}

// Run the optional v0.4.0 rustc helper and use its target facts. The helper is
// an independent process: AMC never links rustc or reads its internal types.
bool query_rust_helper(
    amc::lang::Config &config, llvm::StringRef base, HelperDocument &document, std::string &error, bool &fatal) {
    fatal = false;
    if (config.rust_helper.empty()) return true;
    if (config.files.empty()) {
        error = "[rust] helper requires at least one source file";
        return false;
    }

    llvm::SmallString<256> helper(config.rust_helper);
    if (llvm::sys::path::is_relative(helper)) {
        llvm::SmallString<256> resolved(base);
        llvm::sys::path::append(resolved, helper);
        helper = std::move(resolved);
    }
    llvm::SmallString<256> source(config.files.front());
    if (llvm::sys::path::is_relative(source)) {
        llvm::SmallString<256> resolved(base);
        llvm::sys::path::append(resolved, source);
        source = std::move(resolved);
    }

    llvm::SmallString<64> temporary;
    if (const std::error_code ec = llvm::sys::fs::createTemporaryFile("amc-rust-semantic", "json", temporary)) {
        error = "cannot create rust helper output: " + ec.message();
        return false;
    }
    const std::string crate = llvm::sys::path::stem(source).str();
    const std::string edition = "2021";
    llvm::SmallVector<llvm::StringRef, 10> args;
    args.push_back(helper.str());
    args.push_back("--crate-name");
    args.push_back(crate);
    args.push_back("--edition");
    args.push_back(edition);
    args.push_back("--source");
    args.push_back(source.str());
    args.push_back("--output");
    args.push_back(temporary.str());

    std::string exec_error;
    const int status = llvm::sys::ExecuteAndWait(helper.str(), args, std::nullopt, {}, 0, 0, &exec_error);
    if (status != 0) {
        llvm::sys::fs::remove(temporary);
        error = "rust helper failed";
        if (!exec_error.empty())
            error += ": " + exec_error;
        else
            error += " with status " + std::to_string(status);
        return false;
    }

    auto buffer = llvm::MemoryBuffer::getFile(temporary);
    llvm::sys::fs::remove(temporary);
    if (!buffer) {
        error = "cannot read rust helper output";
        return false;
    }
    amc::Json root;
    std::string parse_error;
    if (!amc::parse_json((*buffer)->getBuffer(), root, parse_error)) {
        error = "invalid rust helper JSON: " + parse_error;
        return false;
    }
    const amc::Json *schema = root.find("schema");
    if (schema == nullptr || schema->as_string() != "amc.rust.semantic/1") {
        error = "rust helper schema mismatch";
        fatal = true;
        return false;
    }
    parse_helper_document(root, document);
    const amc::Json *target = root.find("target");
    if (target == nullptr || !target->is_object()) {
        error = "rust helper output has no target object";
        fatal = true;
        return false;
    }
    const auto number = [&](const char *key) -> int {
        const amc::Json *value = target->find(key);
        return value == nullptr ? -1 : static_cast<int>(value->as_number(-1));
    };
    const int pointer_bits = number("pointer_bits");
    if (pointer_bits != 16 && pointer_bits != 32 && pointer_bits != 64) {
        error = "rust helper target pointer_bits is invalid";
        fatal = true;
        return false;
    }
    const amc::Json *endianness = target->find("endianness");
    const std::string endianness_text = endianness == nullptr ? std::string{} : endianness->as_string();
    const uint32_t endianness_value = endianness_text == "little" ? 1u : (endianness_text == "big" ? 2u : 0u);
    if (endianness_value == 0) {
        error = "rust helper target endianness is invalid";
        fatal = true;
        return false;
    }
    const int c_long_bits = number("c_long_bits");
    if (c_long_bits != 32 && c_long_bits != 64) {
        error = "rust helper target c_long_bits is invalid";
        fatal = true;
        return false;
    }

    if (config.pointer_bits != 0 && config.pointer_bits != static_cast<uint32_t>(pointer_bits)) {
        error = fmt::format(
            "rust helper pointer_bits={} does not match [target] pointer_bits={}", pointer_bits, config.pointer_bits);
        return false;
    }
    if (config.endianness != 0 && config.endianness != endianness_value) {
        error = "rust helper endianness does not match [target] endianness";
        return false;
    }
    if (config.c_long_bits != 0 && config.c_long_bits != static_cast<uint32_t>(c_long_bits)) {
        error = fmt::format(
            "rust helper c_long_bits={} does not match [target] c_long_bits={}", c_long_bits, config.c_long_bits);
        return false;
    }

    if (config.pointer_bits == 0) config.pointer_bits = static_cast<uint32_t>(pointer_bits);
    if (config.endianness == 0) config.endianness = endianness_value;
    if (config.c_long_bits == 0) config.c_long_bits = static_cast<uint32_t>(c_long_bits);
    return true;
}

enum class Tok {
    Ident,
    Number,
    String,
    Punct,
    End
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
};

bool is_ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<Token> lex(const std::string &source) {
    std::vector<Token> tokens;
    const size_t n = source.size();
    size_t i = 0;
    while (i < n) {
        const char c = source[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            while (i < n && source[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && source[i + 1] == '*') {
            int depth = 1;
            i += 2;
            while (i < n && depth > 0) {
                if (source[i] == '/' && i + 1 < n && source[i + 1] == '*') {
                    ++depth;
                    i += 2;
                } else if (source[i] == '*' && i + 1 < n && source[i + 1] == '/') {
                    --depth;
                    i += 2;
                } else {
                    ++i;
                }
            }
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            std::string value;
            while (j < n && source[j] != '"') {
                if (source[j] == '\\' && j + 1 < n) {
                    value += source[j + 1];
                    j += 2;
                } else {
                    value += source[j++];
                }
            }
            tokens.push_back({Tok::String, value});
            i = j < n ? j + 1 : j;
            continue;
        }
        if (c == '\'') {
            // Character literal (`'a'`, `'\n'`) vs. lifetime (`'a`).
            if (i + 2 < n && (source[i + 1] == '\\' || source[i + 2] == '\'')) {
                size_t j = i + 1;
                while (j < n && source[j] != '\'') {
                    if (source[j] == '\\') ++j;
                    ++j;
                }
                i = j < n ? j + 1 : j;
            } else {
                size_t j = i + 1;
                while (j < n && is_ident_char(source[j]))
                    ++j;
                i = j;
            }
            continue;
        }
        if (is_ident_start(c)) {
            size_t j = i;
            while (j < n && is_ident_char(source[j]))
                ++j;
            tokens.push_back({Tok::Ident, source.substr(i, j - i)});
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t j = i;
            while (j < n && (is_ident_char(source[j]) || source[j] == '.'))
                ++j;
            tokens.push_back({Tok::Number, source.substr(i, j - i)});
            i = j;
            continue;
        }
        if (i + 1 < n) {
            const std::string two = source.substr(i, 2);
            if (two == "::" || two == "->" || two == "=>") {
                tokens.push_back({Tok::Punct, two});
                i += 2;
                continue;
            }
        }
        tokens.push_back({Tok::Punct, std::string(1, c)});
        ++i;
    }
    tokens.push_back({Tok::End, {}});
    return tokens;
}

struct RawField {
    std::string name;
    std::vector<std::string> type;
};

struct RawItem {
    enum class Kind {
        Record,
        Enum,
        Alias,
        Union
    } kind = Kind::Record;
    std::string name;
    std::vector<RawField> fields;
    uint32_t align_override = 0;
    uint32_t packed_align = 0;
    bool transparent = false;
    uint32_t enum_size = 4;
    // When set, the layout comes from the rustc helper instead of being
    // recomputed from source tokens.
    bool explicit_layout = false;
    uint64_t explicit_size = 0;
    uint32_t explicit_align = 0;
    std::vector<uint64_t> explicit_offsets;
};

struct RawFunction {
    std::string name;
    std::vector<RawField> parameters;
    std::vector<std::string> return_type;
    bool extern_c = true;
    bool has_receiver = false;
    bool is_unsafe = false;
};

// Spell a parsed Rust type for the generated wrapper source. The parser keeps
// tokens without whitespace, so pointers and references need explicit spacing
// (`*const RustBuffer`, `&mut T`).
std::string sanitize_ident(const std::string &name) {
    std::string out;
    for (const char c : name)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

std::string spell_type(const std::vector<std::string> &tokens) {
    std::string out;
    for (const std::string &token : tokens) {
        const bool tight = token == ","
                        || token == ">"
                        || token == "]"
                        || token == ")"
                        || token == ";"
                        || out.empty()
                        || out.back() == '('
                        || out.back() == '['
                        || out.back() == '*'
                        || out.back() == '&'
                        || out.back() == '\'';
        if (!tight) out += ' ';
        out += token;
    }
    return out;
}

struct Attrs {
    bool repr_c = false;
    uint32_t packed_align = 0;
    bool transparent = false;
    uint32_t align = 0;
    uint32_t int_size = 0;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : tokens_(std::move(tokens)) {}

    bool ok() const { return ok_; }
    const std::string &error() const { return error_; }
    std::vector<RawItem> &items() { return items_; }
    std::vector<RawFunction> &functions() { return functions_; }

    void parse() {
        while (!at_end() && ok_)
            parse_item();
    }

private:
    std::vector<Token> tokens_;
    size_t position_ = 0;
    bool ok_ = true;
    std::string error_;
    std::vector<RawItem> items_;
    std::vector<RawFunction> functions_;

    const Token &peek(size_t offset = 0) const {
        static const Token end{Tok::End, {}};
        return position_ + offset < tokens_.size() ? tokens_[position_ + offset] : end;
    }
    bool at_end() const { return peek().kind == Tok::End; }
    bool check(const char *text, size_t offset = 0) const { return peek(offset).text == text; }
    bool check_kind(Tok kind) const { return peek().kind == kind; }
    void advance() {
        if (!at_end()) ++position_;
    }
    bool accept(const char *text) {
        if (check(text)) {
            advance();
            return true;
        }
        return false;
    }
    bool expect(const char *text, const char *context) {
        if (accept(text)) return true;
        fail(fmt::format("expected '{}' {}", text, context));
        return false;
    }
    void fail(const std::string &message) {
        if (!ok_) return;
        ok_ = false;
        error_ = message;
    }

    void skip_balanced(const char *open, const char *close) {
        if (!accept(open)) return;
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (check(open))
                ++depth;
            else if (check(close))
                --depth;
            advance();
        }
    }

    // Collect a type expression up to (but excluding) a top-level stop token.
    std::vector<std::string> collect_type(const std::vector<const char *> &stops) {
        std::vector<std::string> out;
        int depth = 0;
        while (!at_end()) {
            const Token &token = peek();
            if (depth == 0) {
                bool stop = false;
                for (const char *s : stops)
                    if (token.text == s) stop = true;
                if (stop) break;
            }
            if (token.kind == Tok::Punct) {
                if (token.text == "<" || token.text == "[" || token.text == "(") {
                    ++depth;
                } else if (token.text == ">" || token.text == "]" || token.text == ")") {
                    if (depth == 0) break;
                    --depth;
                }
            }
            out.push_back(token.text);
            advance();
        }
        return out;
    }

    std::string join(const std::vector<std::string> &parts) {
        std::string out;
        for (const auto &part : parts)
            out += part;
        return out;
    }

    Attrs parse_attributes() {
        Attrs attrs;
        while (check("#")) {
            advance();
            if (!accept("[")) {
                fail("malformed attribute");
                return attrs;
            }
            std::vector<std::string> inner;
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (check("["))
                    ++depth;
                else if (check("]"))
                    --depth;
                if (depth == 0) break;
                inner.push_back(peek().text);
                advance();
            }
            expect("]", "to close attribute");
            if (!inner.empty() && inner[0] == "repr") {
                attrs.repr_c = true;
                for (size_t i = 0; i < inner.size(); ++i) {
                    if (inner[i] == "transparent") attrs.transparent = true;
                    if (inner[i] == "packed") {
                        attrs.packed_align = 1;
                        if (i + 2 < inner.size() && inner[i + 1] == "(") {
                            try {
                                attrs.packed_align = static_cast<uint32_t>(std::stoul(inner[i + 2]));
                            } catch (...) {
                                attrs.packed_align = 1;
                            }
                        }
                    }
                    if (inner[i] == "align" && i + 2 < inner.size()) {
                        try {
                            attrs.align = static_cast<uint32_t>(std::stoul(inner[i + 2]));
                        } catch (...) {
                            attrs.align = 0;
                        }
                    }
                    if (inner[i] == "u8")
                        attrs.int_size = 1;
                    else if (inner[i] == "u16" || inner[i] == "i16")
                        attrs.int_size = 2;
                    else if (inner[i] == "u32" || inner[i] == "i32")
                        attrs.int_size = 4;
                    else if (inner[i] == "u64" || inner[i] == "i64")
                        attrs.int_size = 8;
                }
            }
        }
        return attrs;
    }

    void skip_visibility() {
        if (accept("pub")) {
            if (check("(")) skip_balanced("(", ")");
        }
    }

    void parse_item() {
        Attrs attrs = parse_attributes();
        if (!ok_) return;
        skip_visibility();
        bool is_unsafe = false;
        while (check("unsafe") || check("const") || check("async")) {
            if (check("unsafe")) is_unsafe = true;
            advance();
        }

        if (accept("struct")) {
            parse_struct(attrs);
            return;
        }
        if (accept("union")) {
            parse_union(attrs);
            return;
        }
        if (accept("enum")) {
            parse_enum(attrs);
            return;
        }
        if (accept("type")) {
            parse_alias();
            return;
        }
        if (accept("extern")) {
            parse_extern();
            return;
        }
        if (accept("fn")) {
            parse_function(false, is_unsafe);
            return;
        }
        // Unrecognised item (module, impl, use, macro, ...): skip one token.
        advance();
    }

    void parse_generics_if_any() {
        if (check("<")) skip_balanced("<", ">");
    }

    void parse_struct(const Attrs &attrs) {
        if (!check_kind(Tok::Ident)) {
            fail("struct name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Record;
        item.name = peek().text;
        item.align_override = attrs.align;
        item.packed_align = attrs.packed_align;
        item.transparent = attrs.transparent;
        // Only `#[repr(...)]` types have a defined ABI; a default-layout Rust
        // struct must never be recorded (its field order/offsets are unstable).
        const bool accepted = attrs.repr_c;
        advance();
        parse_generics_if_any();
        if (accept(";")) {   // unit struct
            if (accepted) items_.push_back(std::move(item));
            return;
        }
        if (accept("(")) {   // tuple struct
            uint32_t index = 0;
            while (!at_end() && !check(")")) {
                parse_attributes();
                RawField field;
                field.name = fmt::format("field{}", index++);
                field.type = collect_type({",", ")"});
                item.fields.push_back(std::move(field));
                accept(",");
            }
            expect(")", "to close tuple struct");
            accept(";");
            if (accepted) items_.push_back(std::move(item));
            return;
        }
        if (!expect("{", "to open struct body")) return;
        while (!at_end() && !check("}")) {
            parse_attributes();
            skip_visibility();
            if (check("}")) break;
            if (!check_kind(Tok::Ident)) {
                advance();
                continue;
            }
            RawField field;
            field.name = peek().text;
            advance();
            if (!expect(":", "after field name")) return;
            field.type = collect_type({",", "}"});
            item.fields.push_back(std::move(field));
            accept(",");
        }
        expect("}", "to close struct body");
        if (accepted) items_.push_back(std::move(item));
    }

    void parse_union(const Attrs &attrs) {
        if (!check_kind(Tok::Ident)) {
            fail("union name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Union;
        item.name = peek().text;
        item.align_override = attrs.align;
        item.packed_align = attrs.packed_align;
        const bool accepted = attrs.repr_c;
        advance();
        parse_generics_if_any();
        if (!expect("{", "to open union body")) return;
        while (!at_end() && !check("}")) {
            parse_attributes();
            skip_visibility();
            if (check("}")) break;
            if (!check_kind(Tok::Ident)) {
                advance();
                continue;
            }
            RawField field;
            field.name = peek().text;
            advance();
            if (!expect(":", "after union field name")) return;
            field.type = collect_type({",", "}"});
            item.fields.push_back(std::move(field));
            accept(",");
        }
        expect("}", "to close union body");
        if (accepted) items_.push_back(std::move(item));
    }

    void parse_enum(const Attrs &attrs) {
        if (!check_kind(Tok::Ident)) {
            fail("enum name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Enum;
        item.name = peek().text;
        item.enum_size = attrs.int_size == 0 ? 4 : attrs.int_size;
        advance();
        parse_generics_if_any();
        skip_balanced("{", "}");
        if (attrs.repr_c) items_.push_back(std::move(item));
    }

    void parse_alias() {
        if (!check_kind(Tok::Ident)) {
            fail("type alias name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Alias;
        item.name = peek().text;
        advance();
        parse_generics_if_any();
        if (!expect("=", "in type alias")) return;
        RawField underlying;
        underlying.name = "underlying";
        underlying.type = collect_type({";"});
        item.fields.push_back(std::move(underlying));
        accept(";");
        items_.push_back(std::move(item));
    }

    void parse_extern() {
        bool is_c = true;
        if (check_kind(Tok::String)) {
            is_c = peek().text == "C" || peek().text == "c";
            advance();
        }
        if (accept("{")) {
            while (!at_end() && !check("}")) {
                parse_attributes();
                skip_visibility();
                bool is_unsafe = false;
                while (check("unsafe") || check("const")) {
                    if (check("unsafe")) is_unsafe = true;
                    advance();
                }
                if (accept("fn")) {
                    parse_extern_function_signature(is_c, is_unsafe);
                } else {
                    advance();
                }
            }
            expect("}", "to close extern block");
            return;
        }
        // `extern "C" fn ...` definition.
        bool is_unsafe = false;
        while (check("unsafe") || check("const")) {
            if (check("unsafe")) is_unsafe = true;
            advance();
        }
        if (accept("fn")) parse_function(is_c, is_unsafe);
    }

    // A function declaration inside an `extern` block: `fn name(args) -> ret;`
    void parse_extern_function_signature(bool is_c, bool is_unsafe) {
        RawFunction function;
        function.extern_c = is_c;
        function.is_unsafe = is_unsafe;
        if (check_kind(Tok::Ident)) {
            function.name = peek().text;
            advance();
        }
        parse_parameter_list(function.parameters, function.has_receiver);
        if (accept("->")) function.return_type = collect_type({";", "{"});
        accept(";");
        skip_balanced("{", "}");
        if (!function.name.empty()) functions_.push_back(std::move(function));
    }

    void parse_function(bool is_c = false, bool is_unsafe = false) {
        RawFunction function;
        function.extern_c = is_c;
        function.is_unsafe = is_unsafe;
        if (check_kind(Tok::Ident)) {
            function.name = peek().text;
            advance();
        }
        parse_parameter_list(function.parameters, function.has_receiver);
        if (accept("->")) function.return_type = collect_type({";", "{"});
        accept(";");
        skip_balanced("{", "}");
        if (!function.name.empty()) functions_.push_back(std::move(function));
    }

    void parse_parameter_list(std::vector<RawField> &parameters, bool &has_receiver) {
        if (!expect("(", "to open parameter list")) return;
        while (!at_end() && !check(")")) {
            parse_attributes();
            const bool reference_receiver = check("&") && (check("self", 1) || (check("mut", 1) && check("self", 2)));
            if (reference_receiver) {
                // Receiver (`&self`, `&mut self`) — not part of the C ABI.
                has_receiver = true;
                advance();
                accept("mut");
                accept("self");
                if (check(":")) {
                    advance();
                    collect_type({",", ")"});
                }
                accept(",");
                continue;
            }
            if (accept("self")) {
                has_receiver = true;
                accept(",");
                continue;
            }
            if (accept("mut")) {}
            std::vector<std::string> first = collect_type({":", ",", ")"});
            RawField parameter;
            if (check(":")) {
                advance();
                parameter.name = join(first);
                parameter.type = collect_type({",", ")"});
            } else {
                parameter.type = std::move(first);
            }
            parameters.push_back(std::move(parameter));
            accept(",");
        }
        expect(")", "to close parameter list");
    }
};

struct Resolved {
    amc::Hash128 id{};
    uint32_t size = 0;
    uint32_t align = 0;
};

// Builds ABIX IR from the parsed raw items, computing C layout.
class Builder {
public:
    Builder(std::vector<RawItem> items, std::vector<RawFunction> functions, TargetFacts target)
        : items_(std::move(items))
        , functions_(std::move(functions))
        , target_(target) {
        for (size_t i = 0; i < items_.size(); ++i)
            by_name_.emplace(items_[i].name, i);
    }

    amc::AbiModule build(const std::vector<std::string> &wanted, const std::vector<std::string> &explicit_functions) {
        module_.package_name = "rust";
        module_.arch = target_.arch;
        module_.os = target_.os;
        module_.target_abi = target_.target_abi;
        module_.pointer_bits = target_.pointer_bits;
        module_.endianness = target_.endianness;

        for (const auto &name : wanted) {
            if (by_name_.count(name)) {
                resolve_item(name);
                continue;
            }
            for (const auto &function : functions_) {
                if (function.name != name) continue;
                if (!function.extern_c) {
                    set_error("function '" + name + "' is not extern \"C\"; use [[rust.expose]] to generate a wrapper");
                } else {
                    add_function(function);
                }
                break;
            }
        }
        // Explicit [[rust.expose]] functions are selected even when they are not
        // listed in [export] symbols. Wrappers are emitted as `extern "C"` raw
        // functions before this point.
        for (const auto &name : explicit_functions) {
            if (by_name_.count(name)) continue;
            bool matched = false;
            for (const auto &function : functions_)
                if (function.name == name && function.extern_c) {
                    add_function(function);
                    matched = true;
                    break;
                }
            if (!matched) set_error("exposed function not found or not wrappable: " + name);
        }
        return module_;
    }

    bool ok() const { return error_.empty(); }
    const std::string &error() const { return error_; }

private:
    std::vector<RawItem> items_;
    std::vector<RawFunction> functions_;
    std::unordered_map<std::string, size_t> by_name_;
    std::unordered_map<std::string, int> resolved_;
    std::unordered_map<std::string, int> primitive_ids_;
    std::unordered_map<std::string, bool> added_functions_;
    amc::AbiModule module_;
    TargetFacts target_;
    std::string error_;

    static uint32_t round_up(uint32_t value, uint32_t align) {
        return align <= 1 ? value : (value + align - 1) / align * align;
    }

    void set_error(const std::string &message) {
        if (error_.empty()) error_ = message;
    }

    int find_type(amc::Hash128 id) const {
        for (size_t i = 0; i < module_.types.size(); ++i)
            if (module_.types[i].id == id) return static_cast<int>(i);
        return -1;
    }

    bool primitive_spec(const std::string &spelling, amc::PrimitiveAbiKind &kind, uint32_t &width,
        amc::FloatFormat &format, bool &signedness, std::string &descriptor) {
        // Pointer-sized Rust/C spellings follow the target, not the host.
        if (spelling == "isize" || spelling == "usize" || spelling == "c_long" || spelling == "c_ulong") {
            const bool is_signed = spelling == "isize" || spelling == "c_long";
            const bool is_c_long = spelling == "c_long" || spelling == "c_ulong";
            width = is_c_long ? target_.c_long_size : target_.pointer_size;
            kind = is_signed ? amc::PrimitiveAbiKind::sint : amc::PrimitiveAbiKind::uint;
            format = amc::FloatFormat::none;
            signedness = is_signed;
            descriptor = fmt::format("prim:{}{}", is_signed ? 'i' : 'u', width * 8);
            return true;
        }
        struct Spec {
            amc::PrimitiveAbiKind kind;
            uint32_t width;
            amc::FloatFormat format;
            bool is_signed;
            const char *descriptor;
        };
        static const std::unordered_map<std::string, Spec> specs = {
            {       "bool",         {amc::PrimitiveAbiKind::bool_, 1, amc::FloatFormat::none, false, "prim:bool"}},
            {       "char",      {amc::PrimitiveAbiKind::char32, 4, amc::FloatFormat::none, false, "prim:char32"}},
            {         "i8",             {amc::PrimitiveAbiKind::sint, 1, amc::FloatFormat::none, true, "prim:i8"}},
            {        "i16",            {amc::PrimitiveAbiKind::sint, 2, amc::FloatFormat::none, true, "prim:i16"}},
            {        "i32",            {amc::PrimitiveAbiKind::sint, 4, amc::FloatFormat::none, true, "prim:i32"}},
            {        "i64",            {amc::PrimitiveAbiKind::sint, 8, amc::FloatFormat::none, true, "prim:i64"}},
            {       "i128",          {amc::PrimitiveAbiKind::sint, 16, amc::FloatFormat::none, true, "prim:i128"}},
            {         "u8",            {amc::PrimitiveAbiKind::uint, 1, amc::FloatFormat::none, false, "prim:u8"}},
            {        "u16",           {amc::PrimitiveAbiKind::uint, 2, amc::FloatFormat::none, false, "prim:u16"}},
            {        "u32",           {amc::PrimitiveAbiKind::uint, 4, amc::FloatFormat::none, false, "prim:u32"}},
            {        "u64",           {amc::PrimitiveAbiKind::uint, 8, amc::FloatFormat::none, false, "prim:u64"}},
            {       "u128",         {amc::PrimitiveAbiKind::uint, 16, amc::FloatFormat::none, false, "prim:u128"}},
            {        "f32", {amc::PrimitiveAbiKind::floating, 4, amc::FloatFormat::ieee32, false, "prim:fieee32"}},
            {        "f64", {amc::PrimitiveAbiKind::floating, 8, amc::FloatFormat::ieee64, false, "prim:fieee64"}},
            {     "c_char",  {amc::PrimitiveAbiKind::char_signed, 1, amc::FloatFormat::none, true, "prim:char_s"}},
            {    "c_schar",         {amc::PrimitiveAbiKind::schar, 1, amc::FloatFormat::none, true, "prim:schar"}},
            {    "c_uchar",        {amc::PrimitiveAbiKind::uchar, 1, amc::FloatFormat::none, false, "prim:uchar"}},
            {    "c_short",            {amc::PrimitiveAbiKind::sint, 2, amc::FloatFormat::none, true, "prim:i16"}},
            {   "c_ushort",           {amc::PrimitiveAbiKind::uint, 2, amc::FloatFormat::none, false, "prim:u16"}},
            {      "c_int",            {amc::PrimitiveAbiKind::sint, 4, amc::FloatFormat::none, true, "prim:i32"}},
            {     "c_uint",           {amc::PrimitiveAbiKind::uint, 4, amc::FloatFormat::none, false, "prim:u32"}},
            { "c_longlong",            {amc::PrimitiveAbiKind::sint, 8, amc::FloatFormat::none, true, "prim:i64"}},
            {"c_ulonglong",           {amc::PrimitiveAbiKind::uint, 8, amc::FloatFormat::none, false, "prim:u64"}},
            {    "c_float", {amc::PrimitiveAbiKind::floating, 4, amc::FloatFormat::ieee32, false, "prim:fieee32"}},
            {   "c_double", {amc::PrimitiveAbiKind::floating, 8, amc::FloatFormat::ieee64, false, "prim:fieee64"}},
        };
        const auto it = specs.find(spelling);
        if (it == specs.end()) return false;
        kind = it->second.kind;
        width = it->second.width;
        format = it->second.format;
        signedness = it->second.is_signed;
        descriptor = it->second.descriptor;
        return true;
    }

    int add_primitive(amc::PrimitiveAbiKind kind, uint32_t width, amc::FloatFormat format, bool signedness,
        const std::string &descriptor, const std::string &spelling) {
        const auto seen = primitive_ids_.find(descriptor);
        if (seen != primitive_ids_.end()) return seen->second;
        amc::Type type;
        type.name = spelling;
        type.kind = amc::TypeKind::primitive;
        type.id = amc::hash_text(descriptor, kTypeDomain);
        type.size = width;
        type.align = width >= 16 ? 16 : (width == 0 ? 1 : width);
        type.primitive_abi = amc::primitive_abi(width, kind, format, signedness);
        const int index = static_cast<int>(module_.types.size());
        module_.types.push_back(std::move(type));
        primitive_ids_.emplace(descriptor, index);
        return index;
    }

    Resolved resolve_type(const std::vector<std::string> &tokens) {
        if (tokens.empty()) {
            set_error("empty type expression");
            return {};
        }
        // Reference / raw pointer.
        if (tokens[0] == "*" || tokens[0] == "&") {
            size_t i = 1;
            bool is_const = tokens[0] == "&";
            while (i < tokens.size() && (tokens[i] == "const" || tokens[i] == "mut")) {
                if (tokens[i] == "const") is_const = true;
                if (tokens[i] == "mut") is_const = false;
                ++i;
            }
            std::string name;
            for (const auto &token : tokens)
                name += token;
            const Resolved pointer =
                add_aggregate_ref(name, target_.pointer_size, target_.pointer_size, amc::TypeKind::pointer);
            // Record the pointee so element/contract matching uses ABI identity
            // (TypeID) instead of a spelling. An unresolvable pointee falls back
            // to the spelling without failing the parse.
            std::vector<std::string> pointee_tokens(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
            if (!pointee_tokens.empty()) {
                const std::string saved_error = error_;
                error_.clear();
                const Resolved pointee = resolve_type(pointee_tokens);
                if (error_.empty())
                    module_.pointees.push_back({pointer.id, pointee.id, is_const ? amc::pointee_const : 0u});
                else
                    error_ = saved_error;
            }
            return pointer;
        }
        // Fixed-size array `[T; N]`.
        if (tokens[0] == "[" && tokens.back() == "]") {
            size_t semicolon = tokens.size();
            for (size_t i = 1; i < tokens.size(); ++i)
                if (tokens[i] == ";") {
                    semicolon = i;
                    break;
                }
            if (semicolon >= tokens.size() - 1 || semicolon + 1 >= tokens.size() - 1) {
                set_error("malformed array type");
                return {};
            }
            std::vector<std::string> element(tokens.begin() + 1, tokens.begin() + semicolon);
            uint64_t count = 0;
            try {
                count = std::stoull(tokens[semicolon + 1]);
            } catch (...) {
                set_error("malformed array length");
                return {};
            }
            const Resolved inner = resolve_type(element);
            if (!ok()) return {};
            std::string name;
            for (const auto &token : tokens)
                name += token;
            amc::Type type;
            type.name = name;
            type.kind = amc::TypeKind::array;
            type.id = amc::hash_text(name, kTypeDomain);
            type.array_count = static_cast<uint32_t>(count);
            type.size = static_cast<uint32_t>(inner.size * count);
            type.align = inner.align;
            for (const auto &existing : module_.types) {
                if (existing.id == type.id) return {existing.id, existing.size, existing.align};
            }
            const int index = static_cast<int>(module_.types.size());
            module_.types.push_back(std::move(type));
            return {module_.types[static_cast<size_t>(index)].id, module_.types[static_cast<size_t>(index)].size,
                module_.types[static_cast<size_t>(index)].align};
        }
        // `()` -> C void (only meaningful as a return type; not emitted).
        if (tokens.size() == 2 && tokens[0] == "(" && tokens[1] == ")") {
            const int index =
                add_primitive(amc::PrimitiveAbiKind::void_, 0, amc::FloatFormat::none, false, "prim:void", "void");
            return {module_.types[static_cast<size_t>(index)].id, 0, 1};
        }
        std::string spelling;
        for (const auto &token : tokens)
            spelling += token;
        amc::PrimitiveAbiKind kind;
        uint32_t width = 0;
        amc::FloatFormat format = amc::FloatFormat::none;
        bool signedness = false;
        std::string descriptor;
        if (tokens.size() == 1 && primitive_spec(spelling, kind, width, format, signedness, descriptor)) {
            const int index = add_primitive(kind, width, format, signedness, descriptor, spelling);
            return {module_.types[static_cast<size_t>(index)].id, width, width >= 16 ? 16 : (width == 0 ? 1 : width)};
        }
        // Named type.
        if (!by_name_.count(spelling)) {
            set_error("unknown type: " + spelling);
            return {};
        }
        const int index = resolve_item(spelling);
        if (index < 0) return {};
        return {module_.types[static_cast<size_t>(index)].id, module_.types[static_cast<size_t>(index)].size,
            module_.types[static_cast<size_t>(index)].align};
    }

    Resolved add_aggregate_ref(const std::string &name, uint32_t size, uint32_t align, amc::TypeKind kind) {
        const amc::Hash128 id = amc::hash_text(name, kTypeDomain);
        // Pointer spellings repeat across parameters and returns; reuse the
        // first record so the module never carries duplicate TypeIDs.
        for (const auto &type : module_.types) {
            if (type.id == id) return {type.id, type.size, type.align};
        }
        amc::Type type;
        type.name = name;
        type.kind = kind;
        type.id = id;
        type.size = size;
        type.align = align;
        const int index = static_cast<int>(module_.types.size());
        module_.types.push_back(std::move(type));
        return {module_.types[static_cast<size_t>(index)].id, size, align};
    }

    int resolve_item(const std::string &name) {
        const auto cached = resolved_.find(name);
        if (cached != resolved_.end()) {
            if (cached->second < 0) {
                set_error("recursive type without indirection: " + name);
                return -1;
            }
            return cached->second;
        }
        const auto found = by_name_.find(name);
        if (found == by_name_.end()) {
            set_error("unknown type: " + name);
            return -1;
        }
        const RawItem &item = items_[found->second];
        resolved_[name] = -1;   // mark in progress
        if (item.kind == RawItem::Kind::Alias) {
            return finish_alias(item);
        }
        if (item.kind == RawItem::Kind::Union) {
            return finish_union(item);
        }
        if (item.kind == RawItem::Kind::Enum) {
            amc::Type type;
            type.name = item.name;
            type.kind = amc::TypeKind::enumeration;
            type.id = amc::hash_text(item.name, kTypeDomain);
            type.size = item.explicit_layout ? static_cast<uint32_t>(item.explicit_size)
                                             : (item.enum_size == 0 ? 4 : item.enum_size);
            type.align = item.explicit_layout ? (item.explicit_align == 0 ? 1 : item.explicit_align) : type.size;
            const int index = static_cast<int>(module_.types.size());
            module_.types.push_back(std::move(type));
            resolved_[name] = index;
            return index;
        }
        return finish_record(item);
    }

    int finish_union(const RawItem &item) {
        amc::Type type;
        type.name = item.name;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(item.name, kTypeDomain);
        const int index = static_cast<int>(module_.types.size());
        type.field_begin = static_cast<uint32_t>(module_.fields.size());
        module_.types.push_back(type);

        uint32_t size = 0;
        uint32_t align = 1;
        for (const auto &raw : item.fields) {
            const Resolved field_type = resolve_type(raw.type);
            if (!ok()) return -1;
            amc::Field field;
            field.owner_type = type.id;
            field.name = raw.name.empty() ? fmt::format("field{}", module_.fields.size()) : raw.name;
            field.type_id = field_type.id;
            field.offset = 0;
            module_.fields.push_back(std::move(field));
            size = std::max(size, field_type.size);
            align = std::max(align, field_type.align == 0 ? 1 : field_type.align);
        }
        if (item.packed_align != 0) align = std::min(align, item.packed_align);
        if (item.align_override != 0) align = std::max(align, item.align_override);
        if (size == 0) size = 1;
        auto &record = module_.types[static_cast<size_t>(index)];
        record.field_count = static_cast<uint32_t>(module_.fields.size()) - record.field_begin;
        record.size = item.explicit_layout ? static_cast<uint32_t>(item.explicit_size) : round_up(size, align);
        record.align =
            item.explicit_layout ? (item.explicit_align == 0 ? 1 : item.explicit_align) : (align == 0 ? 1 : align);
        std::vector<amc::Field> layout(module_.fields.begin() + record.field_begin, module_.fields.end());
        record.layout_hash = amc::canonical_layout_hash(module_, record, layout);
        resolved_[item.name] = index;
        return index;
    }

    int finish_alias(const RawItem &item) {
        amc::Type type;
        type.name = item.name;
        type.kind = amc::TypeKind::alias;
        type.id = amc::hash_text(item.name, kTypeDomain);
        const int index = static_cast<int>(module_.types.size());
        type.field_begin = static_cast<uint32_t>(module_.fields.size());
        type.field_count = 1;
        module_.types.push_back(type);
        const Resolved underlying =
            resolve_type(item.fields.empty() ? std::vector<std::string>{} : item.fields[0].type);
        if (!ok()) return -1;
        module_.types[static_cast<size_t>(index)].size =
            item.explicit_layout ? static_cast<uint32_t>(item.explicit_size) : underlying.size;
        module_.types[static_cast<size_t>(index)].align = item.explicit_layout
                                                            ? (item.explicit_align == 0 ? 1 : item.explicit_align)
                                                            : (underlying.align == 0 ? 1 : underlying.align);
        module_.fields.push_back({module_.types[static_cast<size_t>(index)].id, "underlying", underlying.id, 0, 0});
        resolved_[item.name] = index;
        return index;
    }

    int finish_record(const RawItem &item) {
        amc::Type type;
        type.name = item.name;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(item.name, kTypeDomain);
        const int index = static_cast<int>(module_.types.size());
        type.field_begin = static_cast<uint32_t>(module_.fields.size());
        module_.types.push_back(type);

        if (item.transparent && item.fields.size() != 1) {
            set_error("repr(transparent) requires exactly one field: " + item.name);
            return -1;
        }
        uint32_t offset = 0;
        uint32_t align = 1;
        for (size_t index = 0; index < item.fields.size(); ++index) {
            const auto &raw = item.fields[index];
            const Resolved field_type = resolve_type(raw.type);
            if (!ok()) return -1;
            const uint32_t natural_align = field_type.align == 0 ? 1 : field_type.align;
            const uint32_t field_align =
                item.packed_align != 0 ? std::min(item.packed_align, natural_align) : natural_align;
            offset = round_up(offset, field_align);
            amc::Field field;
            field.owner_type = type.id;
            field.name = raw.name.empty() ? fmt::format("field{}", module_.fields.size()) : raw.name;
            field.type_id = field_type.id;
            field.offset = item.explicit_layout && index < item.explicit_offsets.size()
                             ? static_cast<uint32_t>(item.explicit_offsets[index])
                             : offset;
            offset += field_type.size;
            align = std::max(align, field_align);
            module_.fields.push_back(std::move(field));
        }
        if (item.packed_align != 0) align = std::min(align, item.packed_align);
        if (item.align_override != 0) align = std::max(align, item.align_override);
        const uint32_t size = round_up(offset, align);
        auto &record = module_.types[static_cast<size_t>(index)];
        record.field_count = static_cast<uint32_t>(module_.fields.size()) - record.field_begin;
        record.size = item.explicit_layout ? static_cast<uint32_t>(item.explicit_size) : (size == 0 ? 1 : size);
        record.align =
            item.explicit_layout ? (item.explicit_align == 0 ? 1 : item.explicit_align) : (align == 0 ? 1 : align);
        std::vector<amc::Field> layout(module_.fields.begin() + record.field_begin, module_.fields.end());
        record.layout_hash = amc::canonical_layout_hash(module_, record, layout);
        resolved_[item.name] = index;
        return index;
    }

    void add_function(const RawFunction &raw) {
        if (!added_functions_.emplace(raw.name, true).second) return;
        const bool void_return =
            raw.return_type.empty()
            || (raw.return_type.size() == 2 && raw.return_type[0] == "(" && raw.return_type[1] == ")");
        amc::Function function;
        function.name = raw.name;
        function.calling_convention = 1;   // C
        if (void_return) {
            const int void_index =
                add_primitive(amc::PrimitiveAbiKind::void_, 0, amc::FloatFormat::none, false, "prim:void", "void");
            function.return_type = module_.types[static_cast<size_t>(void_index)].id;
        } else {
            const Resolved result = resolve_type(raw.return_type);
            if (!ok()) return;
            function.return_type = result.id;
        }
        for (const auto &parameter : raw.parameters) {
            if (parameter.type.empty()) continue;
            if (parameter.type.size() == 2 && parameter.type[0] == "(" && parameter.type[1] == ")") continue;
            const Resolved resolved = resolve_type(parameter.type);
            if (!ok()) return;
            function.parameters.push_back({parameter.name, resolved.id, 0});
        }
        function.signature = amc::signature_hash(function);
        module_.functions.push_back(std::move(function));
    }
};

std::string helper_simple_name(std::string name) {
    if (name.rfind("rust:", 0) == 0) name.erase(0, 5);
    const size_t generic = name.find('<');
    std::string base = generic == std::string::npos ? name : name.substr(0, generic);
    const std::string suffix = generic == std::string::npos ? std::string{} : name.substr(generic);
    const size_t separator = base.rfind("::");
    if (separator != std::string::npos) base.erase(0, separator + 2);
    return base + suffix;
}

std::string helper_type_spelling(std::string text, const std::unordered_map<std::string, std::string> &names) {
    if (text.rfind("rust:", 0) == 0) text.erase(0, 5);
    if (const auto exact = names.find(text); exact != names.end()) return exact->second;
    if (const auto exact = names.find("rust:" + text); exact != names.end()) return exact->second;
    for (const char *prefix : {"*const ", "*mut ", "&mut ", "&"}) {
        const std::string qualifier(prefix);
        if (text.rfind(qualifier, 0) == 0)
            return qualifier + helper_type_spelling(text.substr(qualifier.size()), names);
    }
    return helper_simple_name(text);
}

std::vector<std::string> helper_type_tokens(
    const std::string &text, const std::unordered_map<std::string, std::string> &names) {
    std::vector<Token> tokens = lex(helper_type_spelling(text, names));
    std::vector<std::string> out;
    for (const Token &token : tokens)
        if (token.kind != Tok::End) out.push_back(token.text);
    return out;
}

RawItem::Kind helper_raw_kind(const std::string &kind) {
    if (kind == "union") return RawItem::Kind::Union;
    if (kind == "enum") return RawItem::Kind::Enum;
    if (kind == "alias") return RawItem::Kind::Alias;
    return RawItem::Kind::Record;
}

void apply_helper_repr(const HelperType &type, RawItem &item) {
    for (const std::string &repr : type.repr) {
        if (repr == "transparent") item.transparent = true;
        if (repr.rfind("packed(", 0) == 0) {
            try {
                item.packed_align = static_cast<uint32_t>(std::stoul(repr.substr(7)));
            } catch (...) {
                item.packed_align = 1;
            }
        }
        if (repr.rfind("align(", 0) == 0) {
            try {
                item.align_override = static_cast<uint32_t>(std::stoul(repr.substr(6)));
            } catch (...) {
                item.align_override = 0;
            }
        }
        if (repr == "u8")
            item.enum_size = 1;
        else if (repr == "u16" || repr == "i16")
            item.enum_size = 2;
        else if (repr == "u32" || repr == "i32")
            item.enum_size = 4;
        else if (repr == "u64" || repr == "i64")
            item.enum_size = 8;
    }
}

void merge_helper_types(
    const HelperDocument &document, std::vector<RawItem> &items, std::unordered_map<std::string, std::string> &names) {
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < items.size(); ++i)
        index.emplace(items[i].name, i);
    for (const HelperType &type : document.types)
        names.emplace(type.id, helper_simple_name(type.name.empty() ? type.path : type.name));

    for (const HelperType &type : document.types) {
        if (type.generic) continue;
        const std::string name = helper_simple_name(type.name.empty() ? type.path : type.name);
        auto found = index.find(name);
        if (found == index.end()) {
            RawItem item;
            item.name = name;
            found = index.emplace(name, items.size()).first;
            items.push_back(std::move(item));
        }
        RawItem &item = items[found->second];
        item.kind = helper_raw_kind(type.kind);
        item.explicit_layout = true;
        item.explicit_size = type.size;
        item.explicit_align = type.align;
        item.explicit_offsets.clear();
        apply_helper_repr(type, item);
        if (type.kind == "enum") {
            item.fields.clear();
            continue;
        }
        if (type.kind == "alias") {
            item.fields.clear();
            RawField field;
            field.name = "underlying";
            field.type = helper_type_tokens(type.underlying, names);
            item.fields.push_back(std::move(field));
            continue;
        }
        item.fields.clear();
        for (const HelperField &field : type.fields) {
            RawField raw;
            raw.name = field.name;
            raw.type = helper_type_tokens(field.type, names);
            item.fields.push_back(std::move(raw));
            item.explicit_offsets.push_back(field.offset);
        }
    }
}

void merge_helper_functions(const HelperDocument &document, std::vector<RawFunction> &functions,
    const std::unordered_map<std::string, std::string> &names) {
    std::unordered_map<std::string, bool> seen;
    for (const RawFunction &function : functions)
        seen.emplace(function.name, true);
    for (const HelperFunction &function : document.functions) {
        if (function.has_receiver || function.kind == "method" || function.abi != "C") continue;
        if (!seen.emplace(function.name, true).second) continue;
        RawFunction raw;
        raw.name = function.name;
        raw.extern_c = true;
        for (const HelperField &parameter : function.parameters) {
            RawField field;
            field.name = parameter.name;
            field.type = helper_type_tokens(parameter.type, names);
            raw.parameters.push_back(std::move(field));
        }
        raw.return_type = helper_type_tokens(function.return_type, names);
        functions.push_back(std::move(raw));
    }
}

std::string cross_validate_helper(const HelperDocument &document, const amc::AbiModule &parser_module) {
    std::string mismatches;
    for (const HelperType &type : document.types) {
        if (type.generic) continue;
        const std::string name = helper_simple_name(type.name.empty() ? type.path : type.name);
        const amc::Type *parsed = nullptr;
        for (const amc::Type &candidate : parser_module.types)
            if (candidate.name == name) {
                parsed = &candidate;
                break;
            }
        if (parsed == nullptr) continue;
        if (parsed->size != type.size || parsed->align != type.align) {
            mismatches += fmt::format(
                "{}: parser {}/{} helper {}/{}\n", name, parsed->size, parsed->align, type.size, type.align);
            continue;
        }
        if (type.fields.empty()) continue;
        for (const HelperField &field : type.fields) {
            bool found = false;
            for (uint32_t i = 0; i < parsed->field_count; ++i) {
                const amc::Field &candidate = parser_module.fields[parsed->field_begin + i];
                if (candidate.name == field.name && candidate.offset == field.offset) {
                    found = true;
                    break;
                }
            }
            if (!found) mismatches += fmt::format("{}::{}: offset {} differs\n", name, field.name, field.offset);
        }
    }
    return mismatches;
}

}   // namespace

int run_frontend(const char *config_path, const char *output_path) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::lang::Config config;
    std::string error;
    if (!amc::lang::config_load(config_path, "rust", config, error)) {
        fmt::print(stderr, "{}\n", error);
        return EXIT_FAILURE;
    }
    llvm::SmallString<256> config_path_abs(config_path);
    if (const std::error_code ec = llvm::sys::fs::make_absolute(config_path_abs)) {
        fmt::print(stderr, "cannot resolve {}: {}\n", config_path, ec.message());
        return EXIT_FAILURE;
    }
    const llvm::StringRef base = llvm::sys::path::parent_path(config_path_abs);

    HelperDocument helper;
    if (!config.rust_helper.empty()) {
        std::string helper_error;
        bool helper_fatal = false;
        if (!query_rust_helper(config, base, helper, helper_error, helper_fatal)) {
            if (helper_fatal || config.pointer_bits == 0) {
                fmt::print(stderr, "{}\n", helper_error);
                return EXIT_FAILURE;
            }
            fmt::print(stderr, "warning: {}\n", helper_error);
        }
    }

    TargetFacts target;
    if (!target_facts_from(config, target, error)) {
        fmt::print(stderr, "{}\n", error);
        return EXIT_FAILURE;
    }

    std::vector<RawItem> items;
    std::vector<RawFunction> functions;
    std::unordered_map<std::string, bool> seen_items;
    for (const auto &file : config.files) {
        if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
        llvm::SmallString<256> path(file);
        if (llvm::sys::path::is_relative(path)) {
            llvm::SmallString<256> resolved(base);
            llvm::sys::path::append(resolved, llvm::StringRef(file));
            path = std::move(resolved);
        }
        auto buffer = llvm::MemoryBuffer::getFile(path);
        if (!buffer) {
            fmt::print(stderr, "cannot read {}\n", path.str().str());
            return EXIT_FAILURE;
        }
        std::string source = (*buffer)->getBuffer().str();
        Parser parser(lex(source));
        parser.parse();
        if (!parser.ok()) {
            // The rustc helper is the semantic authority. When it succeeded,
            // advanced syntax the lightweight parser does not model is not a
            // build failure; the helper records those entities instead.
            if (helper.loaded) {
                fmt::print(stderr, "warning: {}: {}\n", path.str().str(), parser.error());
                continue;
            }
            fmt::print(stderr, "{}: {}\n", path.str().str(), parser.error());
            return EXIT_FAILURE;
        }
        for (auto &item : parser.items())
            if (seen_items.emplace(item.name, true).second) items.push_back(std::move(item));
        for (auto &function : parser.functions())
            functions.push_back(std::move(function));
    }
    const std::vector<RawItem> parser_items = items;

    std::vector<std::string> exposed_names;
    std::string wrapper_source;
    std::unordered_map<std::string, bool> exposed_seen;
    std::unordered_map<std::string, size_t> wrapper_item_counts;
    for (const auto &decl : config.rust_exposed) {
        const RawFunction *source = nullptr;
        for (const auto &function : functions) {
            if (function.name == decl.function) {
                source = &function;
                break;
            }
        }
        if (source == nullptr) {
            fmt::print(stderr, "[[rust.expose]] function not found: {}\n", decl.function);
            return EXIT_FAILURE;
        }
        if (source->has_receiver) {
            fmt::print(stderr, "[[rust.expose]] methods are not supported yet: {}\n", decl.function);
            return EXIT_FAILURE;
        }
        const std::string exported = decl.as.empty() ? decl.function : decl.as;
        if (!exposed_seen.emplace(exported, true).second) {
            fmt::print(stderr, "[[rust.expose]] duplicate exported name: {}\n", exported);
            return EXIT_FAILURE;
        }

        // A source function that is already `extern "C"` and keeps its name
        // needs no wrapper. Everything else gets a tiny C-ABI forwarding
        // wrapper so the producer never hand-writes the export.
        const bool needs_wrapper = !source->extern_c || exported != decl.function;
        if (needs_wrapper) {
            // The wrapper's Rust item name must not collide with the source
            // function; `#[export_name]` controls the C symbol instead.
            const std::string base_item = "__amc_export_" + sanitize_ident(exported);
            const size_t occurrence = wrapper_item_counts[base_item]++;
            const std::string item = occurrence == 0 ? base_item : fmt::format("{}_{}", base_item, occurrence);
            wrapper_source += fmt::format("#[export_name = \"{}\"]\npub extern \"C\" fn {}(", exported, item);
            for (size_t i = 0; i < source->parameters.size(); ++i) {
                if (i != 0) wrapper_source += ", ";
                wrapper_source += fmt::format("p{}: {}", i, spell_type(source->parameters[i].type));
            }
            wrapper_source += ")";
            const bool void_return =
                source->return_type.empty()
                || (source->return_type.size() == 2 && source->return_type[0] == "(" && source->return_type[1] == ")");
            if (!void_return) {
                wrapper_source += " -> ";
                wrapper_source += spell_type(source->return_type);
            }
            wrapper_source += " {\n    ";
            if (source->is_unsafe) wrapper_source += "unsafe { ";
            wrapper_source += "crate::";
            wrapper_source += decl.function;
            wrapper_source += "(";
            for (size_t i = 0; i < source->parameters.size(); ++i) {
                if (i != 0) wrapper_source += ", ";
                wrapper_source += fmt::format("p{}", i);
            }
            wrapper_source += ")";
            if (source->is_unsafe) wrapper_source += " }";
            wrapper_source += "\n}\n\n";

            RawFunction wrapper = *source;
            wrapper.name = exported;
            wrapper.extern_c = true;
            wrapper.has_receiver = false;
            functions.push_back(std::move(wrapper));
        }
        exposed_names.push_back(exported);
    }

    if (!wrapper_source.empty()) {
        if (config.thunks_output.empty()) {
            fmt::print(stderr, "[[rust.expose]] requires a [thunks] output path\n");
            return EXIT_FAILURE;
        }
        llvm::SmallString<256> path(base);
        llvm::sys::path::append(path, config.thunks_output);
        std::error_code ec;
        llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
        if (ec) {
            fmt::print(stderr, "cannot write thunk source: {}: {}\n", path.str().str(), ec.message());
            return EXIT_FAILURE;
        }
        out << "// Generated by `amc` - explicit Rust export wrappers for [[rust.expose]].\n"
               "// Compile this file as a module in the same crate as the source functions.\n\n";
        out << wrapper_source;
        out.close();
        if (out.has_error()) {
            fmt::print(stderr, "cannot write thunk source: {}: {}\n", path.str().str(), out.error().message());
            return EXIT_FAILURE;
        }
    }

    std::unordered_map<std::string, std::string> helper_names;
    if (helper.loaded) {
        merge_helper_types(helper, items, helper_names);
        merge_helper_functions(helper, functions, helper_names);
        // The source parser remains an independent implementation. Compare the
        // layouts it derives with the rustc helper for every type both views
        // resolved; the helper layout wins, but a mismatch is a real bug.
        Builder parser_builder(parser_items, {}, target);
        const amc::AbiModule parser_module = parser_builder.build(config.symbols, exposed_names);
        if (parser_builder.ok()) {
            const std::string mismatches = cross_validate_helper(helper, parser_module);
            if (!mismatches.empty())
                fmt::print(stderr, "warning: rust helper layout differs from the source parser:\n{}", mismatches);
        }
    }

    Builder builder(std::move(items), std::move(functions), target);
    amc::AbiModule module = builder.build(config.symbols, exposed_names);
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    if (!builder.ok()) {
        fmt::print(stderr, "{}\n", builder.error());
        return EXIT_FAILURE;
    }
    if (!amc::write_abix(module, output_path, error)) {
        fmt::print(stderr, "{}\n", error);
        return EXIT_FAILURE;
    }

    // Import the ABI entities into Semantic IR and write the sidecar next to
    // the artifact. `synchronize_from_abi` gives every ABI type and function a
    // semantic identity and a direct `abi_type` / `abi_signature` link, so the
    // lowering pass consumes the same facts whether or not the helper ran.
    amc::semantic::SemanticModule semantic;
    semantic.package_name = "rust";
    semantic.rules_version = amc::semantic::semantic_rules_version;
    amc::semantic::synchronize_from_abi(module, semantic);
    const std::string semantic_path = std::string(output_path) + ".semantic";
    if (!amc::semantic::write_semantic_json(semantic, semantic_path, error)) {
        fmt::print(stderr, "{}\n", error);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}   // namespace amc::lang::rust
