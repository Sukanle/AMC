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
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Compilation/Cache.h"
#include "SKL/AMC/Query/Context.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Platform/ELF.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Support/Json.h"
#include "SKL/AMC/Backend/Lua/Lua.h"
#include "SKL/AMC/Artifact/Materialize.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Query/Query.h"
#include "SKL/AMC/Platform/Symbol.h"
#include "SKL/AMC/IR/Verify.h"

#include "SKL/ABIX/Interface/Contract.h"
#include "SKL/ABIX/Model/Compat.h"
#include "SKL/ABIX/Runtime/Registry.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#include <fmt/base.h>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        fmt::print(stderr, "  FAIL: {}\n", message);
        ++failures;
    }
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

skl::amc::Type make_type(const std::string &name, uint32_t size, uint32_t align) {
    skl::amc::Type type;
    type.name = name;
    type.kind = skl::amc::TypeKind::RECORD;
    type.id = skl::amc::hash_text(name, 0x54595045);
    type.size = size;
    type.align = align;
    return type;
}

// When KEEP_ARTIFACTS=1 is set (the CMake option SKL_ABIX_KEEP_TEST_ARTIFACTS),
// the temporary cache/scratch directories are retained for inspection.
bool keep_artifacts() {
    const char *value = std::getenv("KEEP_ARTIFACTS");
    return value != nullptr && std::string(value) == "1";
}

void cleanup_dir(const std::filesystem::path &root) {
    if (keep_artifacts()) {
        fmt::print("  [KEEP] retained artifacts: {}\n", root.string());
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

bool read_binary(const std::filesystem::path &path, std::string &out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0) return false;
    out.assign(static_cast<size_t>(size), '\0');
    return out.empty() || static_cast<bool>(input.read(out.data(), static_cast<std::streamsize>(out.size())));
}

bool write_binary(const std::filesystem::path &path, const std::string &data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(output);
}

// A cache entry is a fixed header, a size table, then the manifest and payload;
// the manifest offset is all a test needs to corrupt a specific region.
size_t cache_entry_manifest_offset(const std::string &blob) {
    if (blob.size() < 24) return blob.size();
    uint32_t count = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        count |= static_cast<uint32_t>(static_cast<unsigned char>(blob[20 + shift / 8])) << shift;
    return 24 + 8ull * count;
}

}   // namespace

int main() {
    // --- .abix round trip -------------------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "test";
        m.types.push_back(make_type("Foo", 8, 8));
        m.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        skl::amc::Function f;
        f.name = "f";
        f.return_type = m.types[0].id;
        f.signature = skl::amc::signature_hash(f);
        m.functions.push_back(f);

        std::string error;
        check(skl::amc::write_abix(m, "amc_core_test.abix", error), "write_abix");
        skl::amc::ABIModule loaded;
        check(skl::amc::read_abix("amc_core_test.abix", loaded, error), "read_abix");
        check(loaded.types.size() == 1, "loaded type count");
        check(loaded.functions.size() == 1, "loaded function count");
        std::remove("amc_core_test.abix");
    }

    // --- unified error schema --------------------------------------------
    {
        const auto error = skl::amc::make_error(skl::amc::ErrorCategory::IO, "read_abix", "cannot open input: x", "x");
        const auto json = skl::amc::error_to_json(error);
        check(contains(json, "\"schema\":\"abix.error/1\""), "error json schema");
        check(contains(json, "\"code\":\"AMC-IO\""), "error json code");
        check(contains(json, "\"category\":\"io\""), "error json category");
        check(contains(json, "\"stage\":\"read_abix\""), "error json stage");
        check(contains(json, "\"file\":\"x\""), "error json file");
        check(contains(json, "\"domain\":\"0x1003000000000001\""), "error json EPCD domain");
        check(contains(json, "\"value\":3"), "error json EPCD value");
        check(error.code.domain == skl::epcd::make_skl_domain(0x003, 0x0001), "error carries the AMC EPCD domain");
        check(error.code.value == static_cast<skl::epcd::ErrorValue>(skl::amc::AMCErr::io),
            "error carries the AMC EPCD value");
        check(skl::amc::error_message(error.code) == "I/O operation failed.", "AMC error message");
        const auto std_error = skl::amc::from_std_errc(std::errc::permission_denied);
        check(std_error.domain == skl::epcd::domain::std_compat, "std::errc uses the EPCD std-compat domain");
        check(std_error.value == static_cast<skl::epcd::ErrorValue>(std::errc::permission_denied),
            "std::errc keeps its numeric identity");
        check(skl::amc::error_message(std_error).find("Permission denied") != std::string::npos,
            "std::errc message is described");
        check(skl::amc::register_errors() == skl::amc::errors_registered(), "AMC error registration is idempotent");
        check(std::string(skl::amc::AMCErr_domain.name) == "AMC", "AMC static domain name");
        check(skl::amc::AMCErr_domain.descriptor.domain == skl::epcd::make_skl_domain(0x003, 0x0001),
            "AMC static domain identity");
        check(contains(skl::amc::error_to_text(error), "amc: io/read_abix: cannot open input: x"),
            "error text rendering");
        skl::amc::ErrorFormat format = skl::amc::ErrorFormat::TEXT;
        check(skl::amc::parse_error_format("json", format) && format == skl::amc::ErrorFormat::JSON,
            "parse_error_format json");
        check(!skl::amc::parse_error_format("yaml", format), "parse_error_format rejects unknown");
    }

    // --- LLM context ------------------------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "demo";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        skl::amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = skl::amc::hash_text("int", 0x54595045);
        field.offset = 0;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;

        const auto llm = skl::amc::context_to_llm(m, true);
        check(contains(llm, "# ABIX ABI context"), "llm header");
        check(contains(llm, "package: demo"), "llm package");
        check(contains(llm, "counts: types=1 fields=1 functions=0 symbols=0"), "llm counts");
        check(contains(llm, "T0 Foo kind=record size=16 align=8"), "llm type record");
        check(contains(llm, "F0 x owner=T0"), "llm field owner index");
        check(!contains(skl::amc::context_to_llm(m, false), "Foo"), "llm --no-names omits names");

        const auto json = skl::amc::context_to_json(m, true);
        check(contains(json, "\"schema\":\"abix.context/1\""), "context json schema");
        check(contains(json, "\"name\":\"Foo\""), "context json type name");
        check(
            !contains(skl::amc::context_to_json(m, false), "\"name\":\"Foo\""), "context json --no-names omits names");
    }

    // --- verify contract vs implementation --------------------------------
    {
        skl::amc::ABIModule contract;
        contract.package_name = "pkg";
        contract.types.push_back(make_type("Foo", 8, 8));
        contract.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        skl::amc::Function fn;
        fn.name = "run";
        fn.owner_type = contract.types[0].id;
        fn.return_type = contract.types[0].id;
        fn.signature = skl::amc::signature_hash(fn);
        contract.functions.push_back(fn);

        skl::amc::ABIModule implementation = contract;
        skl::amc::VerifyResult result;
        std::string error;
        check(skl::amc::verify_modules(contract, implementation, result, error), "verify runs");
        check(result.consistent && result.changes.empty(), "identical modules are consistent");

        implementation.types[0].size = 16;
        implementation.types[0].layout_hash = skl::amc::hash_text("Foo-v2", 0x4c41594f);
        check(skl::amc::verify_modules(contract, implementation, result, error), "verify reruns");
        check(!result.consistent, "layout drift is detected");
        check(result.changes.size() == 1, "one layout change reported");
        check(result.changes[0].kind == skl::amc::ChangeKind::LAYOUT_CHANGED, "layout change kind");
        check(contains(skl::amc::verify_result_to_json(result), "\"status\":\"drift\""), "drift json status");
        check(contains(skl::amc::verify_result_to_text(result), "status=drift"), "drift text status");

        skl::amc::ABIModule missing = contract;
        missing.types.clear();
        missing.functions.clear();
        check(skl::amc::verify_modules(contract, missing, result, error), "verify missing type");
        check(!result.consistent, "missing type is drift");
        check(result.changes[0].kind == skl::amc::ChangeKind::TYPE_REMOVED, "type_removed kind");
    }

    // --- metadata region --------------------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "meta";
        m.package_version = "1.2.3";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        skl::amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = m.types[0].id;
        field.offset = 0;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;

        skl::amc::MetadataOptions options;
        options.build_id = skl::amc::hash_text("build", 1);
        std::vector<uint8_t> region, region_again;
        std::string error;
        check(skl::amc::build_metadata_region(m, options, region, error), "build metadata region");
        check(skl::amc::build_metadata_region(m, options, region_again, error), "rebuild metadata region");
        check(region == region_again, "metadata region is deterministic");

        skl::amc::MetadataHeader header;
        check(skl::amc::read_metadata_header(region, header, error), "read metadata header");
        check(header.type_count == 1 && header.field_count == 1, "metadata counts");
        check(header.build_id == options.build_id, "metadata build id round trip");
        check((header.flags & skl::amc::metadata_flag_value(skl::amc::MetadataFlag::HAS_NAMES)) != 0,
            "metadata has names");
        check(skl::amc::verify_metadata_region(region, error), "verify metadata region");
        check(
            contains(skl::amc::metadata_meta_document(m, header), "metadata_id = \""), "meta document has metadata id");
        check(contains(skl::amc::metadata_header_to_json(header), "\"schema\":\"abix.metadata/1\""),
            "metadata json schema");

        // Stripping names keeps the region valid, only diagnostic strings go.
        skl::amc::MetadataOptions stripped = options;
        stripped.include_names = false;
        std::vector<uint8_t> bare;
        check(skl::amc::build_metadata_region(m, stripped, bare, error), "build stripped region");
        skl::amc::MetadataHeader bare_header;
        check(skl::amc::read_metadata_header(bare, bare_header, error), "read stripped header");
        check((bare_header.flags & skl::amc::metadata_flag_value(skl::amc::MetadataFlag::HAS_NAMES)) == 0,
            "stripped region reports no names");
        check(bare_header.names_size == 0, "stripped region has empty names section");
        check(skl::amc::verify_metadata_region(bare, error), "stripped region still verifies");
        check(!(bare_header.metadata_id == header.metadata_id), "names change metadata id");

        // Tampering with the desc section must break MetadataID.
        std::vector<uint8_t> tampered = region;
        tampered[skl::amc::metadata_manifest_size + 8] ^= 0x01;
        check(!skl::amc::verify_metadata_region(tampered, error), "tampered region fails verification");
    }

    // --- ELF section reader ----------------------------------------------
    {
        const char text[] = "not an elf";
        const auto *bytes = reinterpret_cast<const uint8_t *>(text);
        std::vector<uint8_t> content;
        std::string error;
        check(!skl::amc::is_elf(bytes, sizeof(text) - 1), "non-elf input is rejected");
        check(!skl::amc::find_elf_section(bytes, sizeof(text) - 1, ".abix.metadata", content, error),
            "find_elf_section fails on non-elf input");
        check(contains(error, "not an ELF"), "non-elf error is descriptive");

        // A truncated ELF identification must not read out of bounds.
        const unsigned char header[4] = {0x7f, 'E', 'L', 'F'};
        std::vector<skl::amc::ElfSection> sections;
        check(skl::amc::is_elf(header, sizeof(header)), "ELF magic detected");
        check(!skl::amc::read_elf_sections(header, sizeof(header), sections, error), "truncated ELF is rejected");
    }

    // --- Mach-O section reader --------------------------------------------
    {
        const char text[] = "not a macho";
        const auto *bytes = reinterpret_cast<const uint8_t *>(text);
        std::vector<uint8_t> content;
        std::string error;
        check(!skl::amc::is_macho(bytes, sizeof(text) - 1), "non-macho input is rejected");
        check(!skl::amc::find_macho_section(bytes, sizeof(text) - 1, "__abix_metadata", content, error),
            "find_macho_section fails on non-macho input");
        check(!skl::amc::is_binary(bytes, sizeof(text) - 1), "unrecognized input is not a supported binary");

        // 64-bit Mach-O magic (little-endian) with a truncated header.
        const unsigned char header[4] = {0xcf, 0xfa, 0xed, 0xfe};
        std::vector<skl::amc::BinarySection> sections;
        check(skl::amc::is_macho(header, sizeof(header)), "Mach-O magic detected");
        check(skl::amc::is_binary(header, sizeof(header)), "Mach-O is a supported binary");
        check(skl::amc::detect_binary_format(header, sizeof(header)) == skl::amc::BinaryFormat::MACHO,
            "Mach-O format detected");
        check(!skl::amc::read_macho_sections(header, sizeof(header), sections, error), "truncated Mach-O is rejected");

        check(skl::amc::native_section_name(skl::amc::BinaryFormat::MACHO, ".abix.metadata") == "__abix_metadata",
            "canonical metadata name maps to the Mach-O spelling");
        check(skl::amc::native_section_name(skl::amc::BinaryFormat::MACHO, ".abix.names") == "__abix_names",
            "canonical names section maps to the Mach-O spelling");
        check(skl::amc::native_section_name(skl::amc::BinaryFormat::ELF, ".abix.metadata") == ".abix.metadata",
            "ELF canonical section name is unchanged");

        // `find_binary_section` must report an unsupported container clearly.
        check(!skl::amc::find_binary_section(bytes, sizeof(text) - 1, ".abix.metadata", content, error),
            "find_binary_section rejects an unsupported container");
        check(contains(error, "not a supported binary"), "unsupported container error is descriptive");
    }

    // --- PE reader --------------------------------------------------------
    {
        // Minimal PE32+ with one `.rdata` section and a CodeView (RSDS) debug
        // directory, exercising is_pe / read_pe_sections / read_pe_debug_id.
        std::vector<uint8_t> pe(0x300, 0);
        const auto put16 = [&](size_t o, uint16_t v) {
            pe[o] = static_cast<uint8_t>(v);
            pe[o + 1] = static_cast<uint8_t>(v >> 8);
        };
        const auto put32 = [&](size_t o, uint32_t v) {
            for (int i = 0; i < 4; ++i)
                pe[o + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        const auto put64 = [&](size_t o, uint64_t v) {
            for (int i = 0; i < 8; ++i)
                pe[o + i] = static_cast<uint8_t>(v >> (8 * i));
        };
        pe[0] = 'M';
        pe[1] = 'Z';
        put32(0x3C, 0x40);
        pe[0x40] = 'P';
        pe[0x41] = 'E';
        const size_t pe_coff = 0x44;
        put16(pe_coff + 0, 0x8664);   // amd64
        put16(pe_coff + 2, 1);        // one section
        put16(pe_coff + 16, 240);     // PE32+ optional header
        const size_t pe_optional = pe_coff + 20;
        put16(pe_optional + 0, 0x20B);
        put64(pe_optional + 24, 0x140000000ull);
        const size_t pe_ddir = pe_optional + 112;
        put32(pe_ddir + 6 * 8, 0x1000);   // debug directory RVA
        put32(pe_ddir + 6 * 8 + 4, 28);   // debug directory size
        const size_t pe_sect = pe_optional + 240;
        const char pe_name[] = ".rdata";
        for (int i = 0; i < 6; ++i)
            pe[pe_sect + i] = static_cast<uint8_t>(pe_name[i]);
        put32(pe_sect + 8, 0x100);
        put32(pe_sect + 12, 0x1000);
        put32(pe_sect + 16, 0x200);
        put32(pe_sect + 20, 0x200);
        put32(0x200 + 12, 2);       // IMAGE_DEBUG_TYPE_CODEVIEW
        put32(0x200 + 24, 0x220);   // PointerToRawData -> RSDS record
        const char rsds[] = "RSDS";
        for (int i = 0; i < 4; ++i)
            pe[0x220 + i] = static_cast<uint8_t>(rsds[i]);
        for (int i = 0; i < 16; ++i)
            pe[0x224 + i] = static_cast<uint8_t>(0x10 + i);   // GUID
        put32(0x234, 7);                                      // Age

        std::string pe_error;
        check(skl::amc::is_pe(pe.data(), pe.size()), "PE image detected");
        check(skl::amc::detect_binary_format(pe.data(), pe.size()) == skl::amc::BinaryFormat::PE, "PE format detected");
        std::vector<skl::amc::BinarySection> pe_sections;
        check(skl::amc::read_pe_sections(pe.data(), pe.size(), pe_sections, pe_error), "PE sections read");
        check(skl::amc::native_section_name(skl::amc::BinaryFormat::PE, ".abix.metadata") == ".abix",
            "PE metadata section maps to .abix");
        std::vector<uint8_t> pe_build_id;
        check(skl::amc::read_pe_debug_id(pe.data(), pe.size(), pe_build_id, pe_error), "PE CodeView debug id read");
        check(pe_build_id.size() == 20 && pe_build_id[0] == 0x10 && pe_build_id[16] == 7,
            "PE debug id is the RSDS GUID + Age");
    }

    // --- query engine -----------------------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "q";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types.push_back(make_type("Bar", 8, 8));
        m.functions.push_back(skl::amc::Function{});
        m.functions.back().name = "run";
        m.functions.back().owner_type = m.types[0].id;
        m.functions.back().return_type = m.types[1].id;
        m.functions.back().signature = skl::amc::signature_hash(m.functions.back());

        check(skl::amc::query_type_indices(m, "Foo").size() == 1, "query type exact");
        check(skl::amc::query_type_indices(m, "oo").size() == 1, "query type substring");
        check(skl::amc::query_type_indices(m, "Nope").empty(), "query type no match");
        check(skl::amc::find_type_by_id(m, m.types[1].id) == &m.types[1], "find type by id");
        check(skl::amc::query_function_indices(m, "run").size() == 1, "query function exact");
        check(skl::amc::query_function_indices(m, "Foo::run").size() == 1, "query function qualified");
        check(skl::amc::query_function_indices(m, "nope").empty(), "query function no match");

        const auto indices = skl::amc::query_type_indices(m, "Foo");
        check(contains(skl::amc::query_types_json(m, indices, true, "Foo"), "\"schema\":\"abix.query/1\""),
            "query type json schema");
        check(contains(skl::amc::query_types_text(m, indices, true), "field x")
                  || contains(skl::amc::query_types_text(m, indices, true), "Foo"),
            "query type text");
        check(contains(skl::amc::query_functions_json(m, skl::amc::query_function_indices(m, "run"), "run"),
                  "return_type_name"),
            "query function json");
    }

    // --- symbol store -----------------------------------------------------
    {
        std::string error;
        const uint8_t raw[] = {0xde, 0xad, 0xbe, 0xef};
        check(skl::amc::hex_encode(raw, sizeof(raw)) == "deadbeef", "hex encode");
        std::vector<uint8_t> decoded;
        check(skl::amc::hex_decode("deadbeef", decoded) && decoded.size() == 4 && decoded[0] == 0xde, "hex decode");
        std::string normalized;
        check(skl::amc::normalize_build_id("0xDEADBEEF", normalized) && normalized == "deadbeef", "normalize build id");
        check(!skl::amc::normalize_build_id("xyz", normalized), "normalize rejects non-hex");

        const std::string root = "amc_symbol_test_store";
        const std::string key = "deadbeef";
        const std::string expected_tail = (std::filesystem::path("de") / "adbeef.abix").string();
        check(contains(skl::amc::symbol_store_path(root, key, ".abix"), expected_tail), "symbol store path layout");

        const std::vector<uint8_t> artifact = {1, 2, 3, 4};
        std::string stored;
        check(skl::amc::publish_symbol(root, key, artifact, "meta", ".abix", stored, error), "publish symbol");
        std::vector<uint8_t> fetched;
        std::string fetched_path;
        check(skl::amc::fetch_symbol(root, key, fetched, fetched_path, error), "fetch symbol");
        check(fetched == artifact, "fetched artifact matches");
        cleanup_dir(root);
    }

    // --- JSON -------------------------------------------------------------
    {
        skl::amc::Json value;
        std::string error;
        check(skl::amc::parse_json("{\"a\":[1,2,{\"b\":\"x\\n\"}],\"c\":true,\"d\":null}", value, error), "parse json");
        check(value.find("c") != nullptr && value.find("c")->as_bool(false), "json bool");
        check(value.find("a") != nullptr && value.find("a")->items().size() == 3, "json array");
        check(value.find("d") != nullptr && value.find("d")->is_null(), "json null");
        const std::string dumped = value.dump();
        skl::amc::Json round;
        check(skl::amc::parse_json(dumped, round, error), "reparse json");
        check(round.dump() == dumped, "json round-trips");
        check(!skl::amc::parse_json("{bad}", round, error), "reject invalid json");
        check(!skl::amc::parse_json("{\"a\":1} trailing", round, error), "reject trailing input");

        check(skl::amc::parse_json("// head\n{\"a\": 1, /* middle */}", round, error), "accept json comments");
        check(
            skl::amc::parse_json("[1, 2,]", round, error) && round.items().size() == 2, "accept array trailing comma");
        check(skl::amc::parse_json("{\"a\": 1,}", round, error), "accept object trailing comma");
        check(skl::amc::parse_json("\"foo\\\nbar\"", round, error) && round.as_string() == "foobar",
            "accept LF string continuation");
        check(skl::amc::parse_json("\"foo\\\r\nbar\"", round, error) && round.as_string() == "foobar",
            "accept CRLF string continuation");
        check(!skl::amc::parse_json("\"foo\nbar\"", round, error), "reject raw newline in string");
        check(!skl::amc::parse_json("{'a': 1}", round, error), "reject single quote extension");
        check(!skl::amc::parse_json("{a: 1}", round, error), "reject unquoted key extension");
        check(!skl::amc::parse_json("0x10", round, error), "reject hexadecimal extension");
        check(!skl::amc::parse_json("NaN", round, error), "reject NaN extension");

        skl::amc::Json copied(value);
        copied.set("c", false);
        check(value.find("c")->as_bool(true) && !copied.find("c")->as_bool(true), "json deep copy");
        skl::amc::Json moved(std::move(copied));
        check(moved.find("a") != nullptr, "json move");
        value.set("a", 42);
        check(value.find("a")->as_number() == 42, "json replacement invalidates cache");
        skl::amc::Json numbers = skl::amc::Json::array();
        numbers.push_back(3);
        check(numbers.items().size() == 1 && numbers.items()[0].as_number() == 3, "json array mutation");

        skl::amc::Json pooled;
        check(skl::amc::parse_json("{\"a\":1}", pooled, error), "pooled parse");
        pooled.set("b", 2);
        check(pooled.find("b") != nullptr && pooled.dump() == "{\"a\":1,\"b\":2}", "modify pooled root");

        skl::amc::Json built = skl::amc::Json::object();
        built.set("x", 1);
        built.set("y", "text");
        check(built.dump() == "{\"x\":1,\"y\":\"text\"}", "json serialize object");

        // Regression: Json(unsigned) must serialize as decimal. The library's
        // JSON_LHEX node printed `0x..`, which its own parser (hex disabled)
        // could not read back.
        skl::amc::Json unsigned_value = skl::amc::Json::object();
        unsigned_value.set("count", static_cast<unsigned>(64));
        const std::string unsigned_dump = unsigned_value.dump();
        check(unsigned_dump == "{\"count\":64}", "json unsigned serializes as decimal");
        skl::amc::Json unsigned_round;
        check(skl::amc::parse_json(unsigned_dump, unsigned_round, error)
                  && unsigned_round.find("count") != nullptr
                  && unsigned_round.find("count")->as_number() == 64,
            "json unsigned round-trips");

        // Regression: a find() result must stay valid across later find() calls
        // on the same object, including keys that are prefixes of one another.
        skl::amc::Json many = skl::amc::Json::object();
        many.set("schema", std::string("S"));
        many.set("schema_version", std::string("SV"));
        many.set("request", std::string("R"));
        many.set("request_id", std::string("RI"));
        const skl::amc::Json *schema = many.find("schema");
        const skl::amc::Json *schema_version = many.find("schema_version");
        const skl::amc::Json *request = many.find("request");
        const skl::amc::Json *request_id = many.find("request_id");
        check(schema != nullptr && schema_version != nullptr && request != nullptr && request_id != nullptr,
            "json find finds every key");
        check(schema->as_string() == "S"
                  && schema_version->as_string() == "SV"
                  && request->as_string() == "R"
                  && request_id->as_string() == "RI",
            "json find results are stable across later finds");
        check(many.find("missing") == nullptr, "json find returns null for an absent key");
    }

    // --- region -> module -------------------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "roundtrip";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        skl::amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = m.types[0].id;
        field.offset = 0;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;
        skl::amc::Function function;
        function.name = "run";
        function.owner_type = m.types[0].id;
        function.return_type = m.types[0].id;
        function.signature = skl::amc::signature_hash(function);
        function.params.push_back({"a", m.types[0].id, 0});
        m.functions.push_back(function);

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(skl::amc::build_metadata_region(m, options, region, error), "build region for decode");
        skl::amc::ABIModule decoded;
        check(skl::amc::module_from_region(region, decoded, error), "decode region to module");
        check(decoded.types.size() == 1 && decoded.types[0].name == "Foo", "decoded type name");
        check(decoded.types[0].id == m.types[0].id, "decoded type id");
        check(decoded.fields.size() == 1 && decoded.fields[0].name == "x" && decoded.fields[0].offset == 0,
            "decoded field");
        check(decoded.functions.size() == 1
                  && decoded.functions[0].params.size() == 1
                  && decoded.functions[0].params[0].name == "a",
            "decoded function parameters");
        check(skl::amc::abi_hash(decoded) == skl::amc::abi_hash(m), "decoded module canonical hash matches");

        // Names are optional: decode the stripped variant without names.
        skl::amc::MetadataOptions stripped = options;
        stripped.include_names = false;
        std::vector<uint8_t> bare;
        check(skl::amc::build_metadata_region(m, stripped, bare, error), "build stripped region for decode");
        skl::amc::ABIModule bare_module;
        check(skl::amc::module_from_region(bare, bare_module, error), "decode stripped region");
        check(bare_module.types.size() == 1 && bare_module.types[0].name.empty(),
            "stripped region decodes without names");
        check(bare_module.types[0].id == m.types[0].id && bare_module.types[0].layout_hash == m.types[0].layout_hash,
            "stripped region still carries ABI identity");
    }

    // --- Lua contract generation -----------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "counter";
        skl::amc::Type int_type;
        int_type.name = "int";
        int_type.kind = skl::amc::TypeKind::PRIMITIVE;
        int_type.id = skl::amc::hash_text("int", 0x54595045ULL);
        int_type.size = 4;
        int_type.align = 4;
        int_type.primitive_abi =
            skl::amc::primitive_abi(4, skl::amc::PrimitiveABIKind::AMC_SINT, skl::amc::FloatFormat::NONE, true);
        m.types.push_back(int_type);
        skl::amc::Type counter_type;
        counter_type.name = "Counter";
        counter_type.kind = skl::amc::TypeKind::RECORD;
        counter_type.id = skl::amc::hash_text("Counter", 0x54595045ULL);
        counter_type.size = 4;
        counter_type.align = 4;
        counter_type.field_begin = 0;
        counter_type.field_count = 1;
        m.types.push_back(counter_type);
        skl::amc::Field value;
        value.owner_type = counter_type.id;
        value.name = "value";
        value.type_id = int_type.id;
        value.offset = 0;
        m.fields.push_back(value);
        m.functions.push_back(skl::amc::Function{});
        m.functions.back().name = "counter_add";
        m.functions.back().signature = skl::amc::hash_text("counter_add", 1);
        m.functions.back().params.push_back({"a", skl::amc::Hash128{}, 0});
        m.functions.back().params.push_back({"b", skl::amc::Hash128{}, 0});

        std::string error;
        const std::string contract = skl::amc::generate_lua_contract(m, error);
        check(error.empty(), "lua generation succeeds");
        check(contains(contract, "#include \"Aue/Aue.h\""), "lua contract includes Aue");
        check(contains(contract, "skl::aue::Contract"), "lua contract type");
        check(contains(contract, "\"add\", 2, counter_add"), "lua entry name/arity/symbol");
        check(contains(contract, "\"counter\""), "lua module name");
        check(contains(contract, "skl::aue::Field lua_type_0_fields"), "lua contract emits field descriptors");
        check(contains(contract, "\"value\", 0, 4, skl::aue::FieldKind::INTEGER_SIGNED"), "lua field offset/kind");
        check(contains(contract, "skl::aue::Type lua_types"), "lua contract emits record descriptors");
        check(contains(contract, "\"Counter\", 4, 4, lua_type_0_fields, 1"), "lua record descriptor");
        check(skl::amc::lua_function_symbol("Foo::bar") == "Foo_bar", "lua symbol sanitizes scope");
        check(skl::amc::lua_function_symbol("weird-name") == "weird_name", "lua symbol sanitizes chars");
        check(skl::amc::lua_entry_name("counter", "counter_add") == "add", "lua entry strips package prefix");
        check(skl::amc::lua_entry_name("counter", "other") == "other", "lua entry keeps unprefixed name");

        const std::string conformance = skl::amc::generate_lua_conformance(m, error);
        check(error.empty(), "lua conformance generation succeeds");
        check(contains(conformance, "assert(type(counter[\"add\"])") || contains(conformance, "\"add\""),
            "lua conformance lists the entry");
        check(contains(conformance, "counter._abi_hash"), "lua conformance checks the abi hash");
        check(contains(conformance, "\"counter\""), "lua conformance checks the module name");
    }

    // --- ABI adapter generation -------------------------------------------
    {
        skl::amc::ABIModule v1, v2, report;
        v1.package_name = "v1";
        v2.package_name = "v2";
        const skl::amc::Hash128 int_id = skl::amc::hash_text("int", 0x54595045);
        const skl::amc::Hash128 long_id = skl::amc::hash_text("long", 0x54595045);
        const auto primitive = [](const char *name, skl::amc::Hash128 id, uint32_t size) {
            skl::amc::Type type;
            type.name = name;
            type.kind = skl::amc::TypeKind::PRIMITIVE;
            type.id = id;
            type.size = size;
            type.align = size;
            type.layout_hash = skl::amc::hash_text(std::string(name) + "-layout", 9);
            return type;
        };

        v1.types.push_back(make_type("MapRecord", 8, 4));
        v1.types[0].layout_hash = skl::amc::hash_text("v1-layout", 1);
        v1.types.push_back(primitive("int", int_id, 4));
        skl::amc::Field value;
        value.owner_type = v1.types[0].id;
        value.name = "value";
        value.type_id = int_id;
        value.offset = 0;
        skl::amc::Field count = value;
        count.name = "count";
        count.offset = 4;
        v1.fields = {value, count};
        v1.types[0].field_begin = 0;
        v1.types[0].field_count = 2;

        v2.types.push_back(make_type("MapRecord", 16, 8));
        v2.types[0].layout_hash = skl::amc::hash_text("v2-layout", 2);
        v2.types.push_back(primitive("int", int_id, 4));
        v2.types.push_back(primitive("long", long_id, 8));
        skl::amc::Field copied = value;
        copied.owner_type = v2.types[0].id;
        skl::amc::Field added = copied;
        added.name = "added";
        added.offset = 4;
        skl::amc::Field widened = copied;
        widened.name = "count";
        widened.type_id = long_id;
        widened.offset = 8;
        v2.fields = {copied, added, widened};
        v2.types[0].field_begin = 0;
        v2.types[0].field_count = 3;

        std::string error;
        check(skl::amc::adapter_symbol("a::B") == "a_B", "adapter symbol scope");
        check(skl::amc::adapter_symbol("weird-name") == "weird_name", "adapter symbol chars");

        check(skl::amc::build_compatibility(v1, v2, report, error), "compatibility for adapter");
        check(!report.maps.empty(), "compatibility produced a mapping");
        const std::string adapter = skl::amc::generate_adapter(v1, v2, report, error);
        check(error.empty(), "adapter generation succeeds");
        check(contains(adapter, "namespace abix_adapter"), "adapter namespace");
        check(contains(adapter, "copy_field"), "adapter copy operation");
        check(contains(adapter, "add_default"), "adapter default operation");
        check(contains(adapter, "convert_int"), "adapter integer conversion");

        skl::amc::AdapterOptions typed_options;
        typed_options.typed = true;
        const std::string typed = skl::amc::generate_adapter(v1, v2, report, typed_options, error);
        check(contains(typed, "::skl::abix::bridge::Adapter<"), "typed adapter specialization");
        check(contains(typed, "skl::amc::generated::MapRecord_ABIX"), "typed adapter projection type");
        check(contains(typed, "ABIX_ADAPTER_TYPED"), "typed adapter is guarded");

        skl::amc::AdapterOptions shim_options;
        shim_options.shim = true;
        const std::string shim = skl::amc::generate_adapter(v1, v2, report, shim_options, error);
        check(contains(shim, "extern \"C\""), "shim exports a C ABI");
        check(contains(shim, "abix_adapter_apply"), "shim exposes abix_adapter_apply");
        check(!contains(shim, "#pragma once"), "shim is emitted as a translation unit");

        skl::amc::ABIModule empty;
        std::string missing;
        const std::string none = skl::amc::generate_adapter(empty, empty, empty, missing);
        check(none.empty() && !missing.empty(), "adapter without a mapping reports an error");
    }

    // --- real-world ABI compatibility matrix -----------------------------
    {
        const skl::amc::Hash128 i32 = skl::amc::hash_text("i32", 0x54595045);
        const skl::amc::Hash128 i64 = skl::amc::hash_text("i64", 0x54595045);
        const auto primitive = [](const char *name, skl::amc::Hash128 id, uint32_t size) {
            skl::amc::Type type;
            type.name = name;
            type.kind = skl::amc::TypeKind::PRIMITIVE;
            type.id = id;
            type.size = size;
            type.align = size;
            type.layout_hash = skl::amc::hash_text(std::string(name) + "-layout", 9);
            return type;
        };
        const auto widget = [&](uint32_t size, uint32_t align,
                                std::vector<std::tuple<std::string, skl::amc::Hash128, uint32_t>> fields) {
            skl::amc::ABIModule module;
            module.package_name = "matrix";
            module.types.push_back(make_type("Widget", size, align));
            module.types[0].layout_hash = skl::amc::hash_text("Widget", 0x4c41594f);
            module.types.push_back(primitive("i32", i32, 4));
            module.types.push_back(primitive("i64", i64, 8));
            for (const auto &[name, type_id, offset] : fields) {
                skl::amc::Field field;
                field.owner_type = module.types[0].id;
                field.name = name;
                field.type_id = type_id;
                field.offset = offset;
                module.fields.push_back(std::move(field));
            }
            module.types[0].field_begin = 0;
            module.types[0].field_count = static_cast<uint32_t>(module.fields.size());
            return module;
        };
        const auto has_change = [](const skl::amc::VerifyResult &result, skl::amc::ChangeKind kind) {
            for (const auto &change : result.changes)
                if (change.kind == kind) return true;
            return false;
        };
        const auto has_compatibility = [](const skl::amc::ABIModule &report, skl::amc::CompatibilityKind kind) {
            for (const auto &record : report.compatibility)
                if (record.kind == kind) return true;
            return false;
        };

        const skl::amc::ABIModule base = widget(8, 4,
            {
                {"x", i32, 0},
                {"y", i32, 4}
        });
        const skl::amc::ABIModule inserted = widget(16, 8,
            {
                {       "x", i32, 0},
                {"inserted", i32, 4},
                {       "y", i32, 8}
        });
        const skl::amc::ABIModule removed = widget(4, 4,
            {
                {"x", i32, 0}
        });
        const skl::amc::ABIModule retyped = widget(16, 8,
            {
                {"x", i64, 0},
                {"y", i32, 8}
        });

        skl::amc::VerifyResult verify;
        skl::amc::ABIModule compatibility;
        std::string error;

        check(skl::amc::verify_modules(base, inserted, verify, error), "matrix field insertion verify");
        check(!verify.consistent
                  && has_change(verify, skl::amc::ChangeKind::LAYOUT_CHANGED)
                  && has_change(verify, skl::amc::ChangeKind::FIELD_ADDED),
            "field insertion is layout drift with field_added");
        check(skl::amc::build_compatibility(base, inserted, compatibility, error),
            "matrix field insertion compatibility");
        check(has_compatibility(compatibility, skl::amc::CompatibilityKind::MAP_COMP),
            "field insertion is map_compatible");

        check(skl::amc::verify_modules(base, removed, verify, error), "matrix field deletion verify");
        check(!verify.consistent
                  && has_change(verify, skl::amc::ChangeKind::LAYOUT_CHANGED)
                  && has_change(verify, skl::amc::ChangeKind::FIELD_REMOVED),
            "field deletion is layout drift with field_removed");
        check(
            skl::amc::build_compatibility(base, removed, compatibility, error), "matrix field deletion compatibility");
        check(has_compatibility(compatibility, skl::amc::CompatibilityKind::MAP_COMP),
            "field deletion is map_compatible");

        check(skl::amc::verify_modules(base, retyped, verify, error), "matrix field type change verify");
        check(!verify.consistent
                  && has_change(verify, skl::amc::ChangeKind::LAYOUT_CHANGED)
                  && has_change(verify, skl::amc::ChangeKind::FIELD_TYPE_CHANGED),
            "field type change is layout drift with field_type_changed");
        check(skl::amc::build_compatibility(base, retyped, compatibility, error),
            "matrix field type change compatibility");
        check(has_compatibility(compatibility, skl::amc::CompatibilityKind::MAP_COMP),
            "integer field widening is map_compatible");

        skl::amc::ABIModule cc_before = base;
        skl::amc::ABIModule cc_after = base;
        skl::amc::Function get;
        get.name = "widget_get";
        get.return_type = i32;
        get.signature = skl::amc::hash_text("widget_get", 0x5349474e41545552ULL);
        get.calling_convention = 1;
        cc_before.functions.push_back(get);
        cc_after.functions.push_back(get);
        cc_after.functions[0].calling_convention = 2;
        check(skl::amc::verify_modules(cc_before, cc_after, verify, error), "matrix calling convention verify");
        check(!verify.consistent && has_change(verify, skl::amc::ChangeKind::CALLING_CONVENTION_CHANGED),
            "calling convention change is reported as drift");

        skl::amc::ABIModule owned_before = base;
        skl::amc::ABIModule owned_after = base;
        skl::amc::Function use;
        use.name = "widget_use";
        use.return_type = i32;
        use.signature = skl::amc::hash_text("widget_use", 0x5349474e41545552ULL);
        use.params.push_back({"value", i32, 0});
        skl::amc::Function destroy;
        destroy.name = "widget_destroy";
        destroy.return_type = i32;
        destroy.signature = skl::amc::hash_text("widget_destroy", 0x5349474e41545552ULL);
        for (skl::amc::ABIModule *module : {&owned_before, &owned_after}) {
            module->functions.push_back(use);
            module->functions.push_back(destroy);
        }
        skl::amc::OwnershipContract ownership;
        ownership.target = skl::amc::OwnershipTarget::PARAM;
        ownership.target_index = 0;
        ownership.mode = skl::amc::OwnershipMode::BORROWED;
        owned_before.contracts.push_back(ownership);
        ownership.mode = skl::amc::OwnershipMode::OWNED;
        ownership.release = destroy.signature;
        owned_after.contracts.push_back(ownership);
        check(skl::amc::verify_modules(owned_before, owned_after, verify, error), "matrix ownership verify");
        check(!verify.consistent && has_change(verify, skl::amc::ChangeKind::OWNERSHIP_CHANGED),
            "ownership mode mismatch is reported as contract drift");

        skl::amc::ErrorContract error_boundary;
        error_boundary.function_index = 0;
        error_boundary.domain = 0x1003000000000001ULL;
        error_boundary.status_source = skl::amc::StatusSource::RETURN_VALUE;
        error_boundary.success_value = 0;
        owned_before.errors.push_back(error_boundary);
        error_boundary.success_value = 1;
        owned_after.errors.push_back(error_boundary);
        check(skl::amc::verify_modules(owned_before, owned_after, verify, error), "matrix error boundary verify");
        check(!verify.consistent && has_change(verify, skl::amc::ChangeKind::ERROR_BOUNDARY_CHANGED),
            "error boundary mismatch is reported as contract drift");
    }

    // --- region -> runtime projection ------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "proj";
        m.package_version = "1";
        m.types.push_back(make_type("Foo", 16, 8));
        m.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        skl::amc::Field field;
        field.owner_type = m.types[0].id;
        field.name = "x";
        field.type_id = m.types[0].id;
        field.offset = 8;
        m.fields.push_back(field);
        m.types[0].field_begin = 0;
        m.types[0].field_count = 1;

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(skl::amc::build_metadata_region(m, options, region, error), "region for projection");

        skl::amc::MaterializedModule materialized;
        check(materialized.build_from_region(region, error), "materialize region");
        check(materialized.descriptor.type_count == 1, "materialized type count");
        const skl::abix::Hash128 runtime_id{m.types[0].id.lo, m.types[0].id.hi};
        check(materialized.descriptor.types[0].type_id == runtime_id, "materialized type id");

        skl::abix::runtime::Registry<8> registry;
        check(registry.register_module(materialized.descriptor) == skl::abix::runtime::RegisterStatus::ok,
            "register materialized module");
        const auto *entry = registry.find_by_id(runtime_id);
        check(entry != nullptr, "materialized type is registered");
        check(entry != nullptr && entry->descriptor->field_count == 1, "materialized field count");
        check(entry != nullptr && entry->descriptor->fields[0].offset == 8, "materialized field offset");
    }

    // --- source origin ----------------------------------------------------
    {
        skl::amc::ABIModule m;
        m.package_name = "src";
        m.types.push_back(make_type("Foo", 8, 8));
        m.types[0].layout_hash = skl::amc::hash_text("Foo", 0x4c41594f);
        m.sources.push_back({m.types[0].id, "/tmp/foo.hpp", 12, 3});

        std::string error;
        check(skl::amc::write_abix(m, "amc_source_test.abix", error), "write with source origin");
        skl::amc::ABIModule loaded;
        check(skl::amc::read_abix("amc_source_test.abix", loaded, error), "read with source origin");
        check(loaded.sources.size() == 1, "source origin round-trips");
        const skl::amc::SourceOrigin *origin = skl::amc::find_source_origin(loaded, m.types[0].id);
        check(origin != nullptr && origin->file == "/tmp/foo.hpp" && origin->line == 12 && origin->column == 3,
            "source origin fields");
        check(skl::amc::abi_hash(loaded) == skl::amc::abi_hash(m), "source origin excluded from abi hash");
        std::remove("amc_source_test.abix");
    }

    {
        // Region extension directory: discovery, boundary rules and the
        // treatment of unknown versions/kinds.
        skl::amc::ABIModule m;
        m.package_name = "ext";
        m.package_version = "1";
        skl::amc::Type t;
        t.name = "Foo";
        t.kind = skl::amc::TypeKind::RECORD;
        t.id = skl::amc::hash_text("Foo", 0x54595045);
        t.size = 4;
        t.align = 4;
        m.types.push_back(t);
        skl::amc::Function f;
        f.name = "f";
        f.return_type = t.id;
        m.functions.push_back(f);
        skl::amc::OwnershipContract contract;
        contract.target = skl::amc::OwnershipTarget::RETURN;
        contract.target_index = 0;
        contract.mode = skl::amc::OwnershipMode::BORROWED;
        m.contracts.push_back(contract);

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(skl::amc::build_metadata_region(m, options, region, error), "build region with a contract");
        std::vector<skl::amc::MetadataOwnershipContract> owned;
        check(skl::amc::read_ownership_contracts(region, owned, error)
                  && owned.size() == 1
                  && owned[0].mode == skl::amc::OwnershipMode::BORROWED,
            "ownership contract discovered through the directory");

        skl::amc::MetadataHeader header;
        check(skl::amc::read_metadata_header(region, header, error), "read region header");
        const size_t known = static_cast<size_t>(header.type_count) * 64
                           + static_cast<size_t>(header.field_count) * 48
                           + static_cast<size_t>(header.function_count) * 72
                           + static_cast<size_t>(header.parameter_count) * 32
                           + static_cast<size_t>(header.symbol_count) * 16;
        check(header.desc_size > known, "region carries an extension directory");
        const size_t directory = static_cast<size_t>(header.desc_offset) + known;

        std::vector<skl::amc::MetadataOwnershipContract> ignored;
        std::vector<uint8_t> bad_version = region;
        bad_version[directory] = 99;
        check(!skl::amc::read_ownership_contracts(bad_version, ignored, error), "unknown directory version rejected");

        std::vector<uint8_t> bad_bounds = region;
        bad_bounds[directory + 16 + 12] = 0xff;   // first entry byte_offset low byte -> out of range
        bad_bounds[directory + 16 + 13] = 0xff;
        check(
            !skl::amc::read_ownership_contracts(bad_bounds, ignored, error), "out-of-bounds extension entry rejected");

        std::vector<uint8_t> unknown_kind = region;
        unknown_kind[directory + 16] = 0x7f;   // first entry kind -> unknown
        check(skl::amc::read_ownership_contracts(unknown_kind, ignored, error) && ignored.empty(),
            "unknown extension kind is skipped");
    }

    {
        // Target ABI verification: compatible / incompatible / unverifiable.
        skl::amc::MetadataOptions options;
        std::string error;
        skl::amc::Type t;
        t.name = "Foo";
        t.kind = skl::amc::TypeKind::RECORD;
        t.id = skl::amc::hash_text("Foo", 0x54595045);
        t.size = 4;
        t.align = 4;

        skl::amc::ABIModule m;
        m.package_name = "target";
        m.package_version = "1";
        m.arch = 1;
        m.os = 1;
        m.target_abi = 1;
        m.pointer_bits = 64;
        m.endianness = 1;
        m.types.push_back(t);
        std::vector<uint8_t> region;
        check(skl::amc::build_metadata_region(m, options, region, error), "build region with target");
        skl::amc::TargetDescription target;
        check(skl::amc::read_target_description(region, target, error) && target.present && target.pointer_bits == 64,
            "read target description");

        skl::amc::TargetExpectation expected;
        expected.arch = 1;
        expected.os = 1;
        expected.target_abi = 1;
        expected.pointer_bits = 64;
        expected.endianness = 1;
        skl::amc::TargetVerification verdict;
        check(skl::amc::verify_target_abi(region, expected, verdict, error)
                  && verdict.status == skl::amc::TargetVerificationStatus::COMPATIBLE,
            "target compatible");
        skl::amc::TargetExpectation wrong = expected;
        wrong.arch = 2;
        check(skl::amc::verify_target_abi(region, wrong, verdict, error)
                  && verdict.status == skl::amc::TargetVerificationStatus::INCOMPATIBLE,
            "target incompatible");

        // No target extension -> unverifiable, never compatible.
        skl::amc::ABIModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(t);
        std::vector<uint8_t> bare_region;
        check(skl::amc::build_metadata_region(bare, options, bare_region, error), "build region without target");
        skl::amc::TargetExpectation any;
        any.arch = 1;
        check(skl::amc::verify_target_abi(bare_region, any, verdict, error)
                  && verdict.status == skl::amc::TargetVerificationStatus::UNVERIFIABLE,
            "missing target is unverifiable");

        // Target present but a checked fact is unknown -> unverifiable.
        skl::amc::ABIModule partial;
        partial.package_name = "partial";
        partial.package_version = "1";
        partial.arch = 1;
        partial.types.push_back(t);
        std::vector<uint8_t> partial_region;
        check(skl::amc::build_metadata_region(partial, options, partial_region, error), "build partial target region");
        check(skl::amc::verify_target_abi(partial_region, expected, verdict, error)
                  && verdict.status == skl::amc::TargetVerificationStatus::UNVERIFIABLE,
            "unknown target field is unverifiable");
    }

    {
        // ABIX-STD sequence-view lowering: shape alone is not compatibility.
        skl::amc::ABIModule m;
        m.pointer_bits = 64;
        const auto add_pointer = [&](const char *name) {
            skl::amc::Type t;
            t.name = name;
            t.kind = skl::amc::TypeKind::POINTER;
            t.id = skl::amc::hash_text(name, 0x54595045);
            t.size = 8;
            t.align = 8;
            m.types.push_back(t);
            return t.id;
        };
        const auto add_primitive = [&](const char *name, uint32_t width, skl::amc::PrimitiveABIKind kind) {
            skl::amc::Type t;
            t.name = name;
            t.kind = skl::amc::TypeKind::PRIMITIVE;
            t.id = skl::amc::hash_text(name, 0x54595045);
            t.size = width;
            t.align = width;
            t.primitive_abi = skl::amc::primitive_abi(width, kind);
            m.types.push_back(t);
            return t.id;
        };
        const auto add_record = [&](const char *name,
                                    const std::vector<std::tuple<std::string, skl::amc::Hash128, uint32_t>> &fields) {
            skl::amc::Type t;
            t.name = name;
            t.kind = skl::amc::TypeKind::RECORD;
            t.id = skl::amc::hash_text(name, 0x54595045);
            t.align = 8;
            t.field_begin = static_cast<uint32_t>(m.fields.size());
            t.field_count = static_cast<uint32_t>(fields.size());
            for (const auto &field : fields) {
                m.fields.push_back({t.id, std::get<0>(field), std::get<1>(field), std::get<2>(field), 0});
                t.size = std::max(t.size, std::get<2>(field) + 8);
            }
            m.types.push_back(t);
            return t.id;
        };
        const auto int_id = add_primitive("int", 4, skl::amc::PrimitiveABIKind::AMC_SINT);
        const auto float_id = add_primitive("float", 4, skl::amc::PrimitiveABIKind::AMC_FLOAT);
        const auto u64 = add_primitive("unsigned long", 8, skl::amc::PrimitiveABIKind::AMC_UINT);
        const auto i64 = add_primitive("long", 8, skl::amc::PrimitiveABIKind::AMC_SINT);
        const auto int_ptr = add_pointer("const int*");
        const auto float_ptr = add_pointer("const float*");
        // Record the pointee so element matching is by TypeID, not spelling.
        m.pointees.push_back({int_ptr, int_id, skl::amc::pointee_const});
        m.pointees.push_back({float_ptr, float_id, skl::amc::pointee_const});
        const auto span = add_record("span", {
                                                 {"data", int_ptr, 0},
                                                 { "len",     u64, 8}
        });
        const auto reversed = add_record("reversed", {
                                                         { "len",     u64, 0},
                                                         {"data", int_ptr, 8}
        });
        const auto wrong_element = add_record("wrong_element", {
                                                                   {"data", float_ptr, 0},
                                                                   { "len",       u64, 8}
        });
        const auto bad_length = add_record("bad_length", {
                                                             {"data", int_ptr, 0},
                                                             { "len",     i64, 8}
        });
        const auto extra_field =
            add_record("extra_field", {
                                          {"data", int_ptr,  0},
                                          { "len",     u64,  8},
                                          { "cap",     u64, 16}
        });

        const auto direct = skl::amc::evaluate_sequence_view(m, span, int_id, "int", false);
        check(
            direct.status == static_cast<uint32_t>(skl::amc::LoweringStatus::DIRECT), "span is a direct sequence view");

        const auto element = skl::amc::evaluate_sequence_view(m, wrong_element, int_id, "int", false);
        check(element.status == static_cast<uint32_t>(skl::amc::LoweringStatus::UNSUPPORTED)
                  && element.reason == static_cast<uint32_t>(skl::amc::LoweringReason::ELEMENT_MISMATCH),
            "element mismatch is rejected (by pointee TypeID)");

        const auto adapter = skl::amc::evaluate_sequence_view(m, reversed, int_id, "int", false);
        check(adapter.status == static_cast<uint32_t>(skl::amc::LoweringStatus::NEEDS_ADAPTER),
            "reversed layout needs an adapter");

        const auto length = skl::amc::evaluate_sequence_view(m, bad_length, int_id, "int", false);
        check(length.status == static_cast<uint32_t>(skl::amc::LoweringStatus::UNSUPPORTED)
                  && length.reason == static_cast<uint32_t>(skl::amc::LoweringReason::LENGTH_TYPE),
            "non-unsigned length is rejected");

        const auto extra = skl::amc::evaluate_sequence_view(m, extra_field, int_id, "int", false);
        check(extra.status == static_cast<uint32_t>(skl::amc::LoweringStatus::UNSUPPORTED)
                  && extra.reason == static_cast<uint32_t>(skl::amc::LoweringReason::FIELD_COUNT),
            "an extra field is rejected");

        const auto mutable_view = skl::amc::evaluate_sequence_view(m, span, int_id, "int", true);
        check(mutable_view.status == static_cast<uint32_t>(skl::amc::LoweringStatus::UNSUPPORTED)
                  && mutable_view.reason == static_cast<uint32_t>(skl::amc::LoweringReason::MUTABILITY_MISMATCH),
            "a const pointer cannot satisfy a mutable contract");
    }

    {
        // ABIX-STD string-view: { data, length, encoding }, code unit by TypeID.
        skl::amc::ABIModule m;
        m.pointer_bits = 64;
        const auto add_pointer = [&](const char *name) {
            skl::amc::Type t;
            t.name = name;
            t.kind = skl::amc::TypeKind::POINTER;
            t.id = skl::amc::hash_text(name, 0x54595045);
            t.size = 8;
            t.align = 8;
            m.types.push_back(t);
            return t.id;
        };
        const auto add_primitive = [&](const char *name, uint32_t width, skl::amc::PrimitiveABIKind kind) {
            skl::amc::Type t;
            t.name = name;
            t.kind = skl::amc::TypeKind::PRIMITIVE;
            t.id = skl::amc::hash_text(name, 0x54595045);
            t.size = width;
            t.align = width;
            t.primitive_abi = skl::amc::primitive_abi(width, kind);
            m.types.push_back(t);
            return t.id;
        };
        const auto add_record = [&](const char *name,
                                    const std::vector<std::tuple<std::string, skl::amc::Hash128, uint32_t>> &fields) {
            skl::amc::Type t;
            t.name = name;
            t.kind = skl::amc::TypeKind::RECORD;
            t.id = skl::amc::hash_text(name, 0x54595045);
            t.align = 8;
            t.field_begin = static_cast<uint32_t>(m.fields.size());
            t.field_count = static_cast<uint32_t>(fields.size());
            for (const auto &field : fields) {
                m.fields.push_back({t.id, std::get<0>(field), std::get<1>(field), std::get<2>(field), 0});
                t.size = std::max(t.size, std::get<2>(field) + 8);
            }
            m.types.push_back(t);
            return t.id;
        };
        const auto ch = add_primitive("char", 1, skl::amc::PrimitiveABIKind::AMC_CHAR_SIGNED);
        const auto ch_ptr = add_pointer("const char*");
        m.pointees.push_back({ch_ptr, ch, skl::amc::pointee_const});
        const auto u64 = add_primitive("unsigned long", 8, skl::amc::PrimitiveABIKind::AMC_UINT);
        const auto u32 = add_primitive("unsigned int", 4, skl::amc::PrimitiveABIKind::AMC_UINT);
        const auto text = add_record("text", {
                                                 {    "data", ch_ptr,  0},
                                                 {     "len",    u64,  8},
                                                 {"encoding",    u32, 16}
        });
        const auto no_encoding = add_record("no_encoding", {
                                                               {"data", ch_ptr, 0},
                                                               { "len",    u64, 8}
        });

        const auto direct = skl::amc::evaluate_string_view(m, text, ch, "char", false);
        check(direct.status == static_cast<uint32_t>(skl::amc::LoweringStatus::DIRECT), "string view is direct");

        const auto missing = skl::amc::evaluate_string_view(m, no_encoding, ch, "char", false);
        check(missing.status == static_cast<uint32_t>(skl::amc::LoweringStatus::UNSUPPORTED)
                  && missing.reason == static_cast<uint32_t>(skl::amc::LoweringReason::FIELD_COUNT),
            "string view without an encoding is rejected");

        const auto wrong = skl::amc::evaluate_string_view(m, text, u64, "unsigned long", false);
        check(wrong.status == static_cast<uint32_t>(skl::amc::LoweringStatus::UNSUPPORTED)
                  && wrong.reason == static_cast<uint32_t>(skl::amc::LoweringReason::ELEMENT_MISMATCH),
            "string view code unit mismatch is rejected");
    }

    {
        // The lowering result round-trips through the metadata region extension.
        skl::amc::ABIModule m;
        m.package_name = "lower";
        m.package_version = "1";
        m.pointer_bits = 64;
        skl::amc::Type span;
        span.name = "span";
        span.kind = skl::amc::TypeKind::RECORD;
        span.id = skl::amc::hash_text("span", 0x54595045);
        span.size = 16;
        span.align = 8;
        skl::amc::Type rev;
        rev.name = "rev";
        rev.kind = skl::amc::TypeKind::RECORD;
        rev.id = skl::amc::hash_text("rev", 0x54595045);
        rev.size = 16;
        rev.align = 8;
        m.types.push_back(span);
        m.types.push_back(rev);
        skl::amc::LoweringResult lowering;
        lowering.type_id = rev.id;
        lowering.contract_type = span.id;
        lowering.kind = static_cast<uint32_t>(skl::amc::LoweringKind::SEQUENCE_VIEW);
        lowering.status = static_cast<uint32_t>(skl::amc::LoweringStatus::NEEDS_ADAPTER);
        lowering.reason = static_cast<uint32_t>(skl::amc::LoweringReason::LAYOUT);
        lowering.pointer_field = 1;
        lowering.length_field = 0;
        m.lowerings.push_back(lowering);

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(skl::amc::build_metadata_region(m, options, region, error), "build region with a lowering");
        std::vector<skl::amc::MetadataLoweringContract> lowerings;
        check(skl::amc::read_lowering_contracts(region, lowerings, error)
                  && lowerings.size() == 1
                  && lowerings[0].status == skl::amc::LoweringStatus::NEEDS_ADAPTER
                  && lowerings[0].reason == skl::amc::LoweringReason::LAYOUT
                  && lowerings[0].type_index == 1
                  && lowerings[0].contract_index == 0,
            "lowering round-trips through the region");

        // A region with no lowering keeps the same bytes as before the extension.
        skl::amc::ABIModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(span);
        std::vector<uint8_t> bare_region;
        check(skl::amc::build_metadata_region(bare, options, bare_region, error), "build region without a lowering");
        std::vector<skl::amc::MetadataLoweringContract> none;
        check(skl::amc::read_lowering_contracts(bare_region, none, error) && none.empty(),
            "no lowering means an empty read");
    }

    {
        // Structured ABI verification: platform / layout / calling convention.
        skl::amc::ABIModule m;
        m.package_name = "abi";
        m.package_version = "1";
        m.arch = 1;
        m.os = 1;
        m.target_abi = 1;
        m.pointer_bits = 64;
        m.endianness = 1;
        skl::amc::Type record;
        record.name = "Foo";
        record.kind = skl::amc::TypeKind::RECORD;
        record.id = skl::amc::hash_text("Foo", 0x54595045);
        record.size = 16;
        record.align = 8;
        record.field_begin = 0;
        record.field_count = 1;
        skl::amc::Type length;
        length.name = "unsigned long";
        length.kind = skl::amc::TypeKind::PRIMITIVE;
        length.id = skl::amc::hash_text("unsigned long", 0x54595045);
        length.size = 8;
        length.align = 8;
        length.primitive_abi = skl::amc::primitive_abi(8, skl::amc::PrimitiveABIKind::AMC_UINT);
        m.types.push_back(record);
        m.types.push_back(length);
        m.fields.push_back({record.id, "len", length.id, 0, 0});
        skl::amc::Function function;
        function.name = "f";
        function.return_type = length.id;
        function.calling_convention = 1;
        function.signature = skl::amc::signature_hash(function);
        m.functions.push_back(function);

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        std::string error;
        check(skl::amc::build_metadata_region(m, options, region, error), "build region for ABI verification");

        skl::amc::TargetExpectation expected;
        expected.arch = 1;
        expected.os = 1;
        expected.target_abi = 1;
        expected.pointer_bits = 64;
        expected.endianness = 1;
        expected.calling_convention = 1;
        skl::amc::AbiVerificationReport report;
        check(skl::amc::verify_abi(region, expected, report, error)
                  && report.platform == skl::amc::TargetVerificationStatus::COMPATIBLE
                  && report.layout == skl::amc::TargetVerificationStatus::COMPATIBLE
                  && report.calling_convention == skl::amc::TargetVerificationStatus::COMPATIBLE,
            "ABI verification: all dimensions compatible");

        skl::amc::TargetExpectation wrong_arch = expected;
        wrong_arch.arch = 2;
        check(skl::amc::verify_abi(region, wrong_arch, report, error)
                  && report.platform == skl::amc::TargetVerificationStatus::INCOMPATIBLE,
            "ABI verification: platform incompatible");

        skl::amc::TargetExpectation wrong_cc = expected;
        wrong_cc.calling_convention = 2;
        check(skl::amc::verify_abi(region, wrong_cc, report, error)
                  && report.calling_convention == skl::amc::TargetVerificationStatus::INCOMPATIBLE,
            "ABI verification: calling convention incompatible");

        // A region without a target description: platform is unverifiable.
        skl::amc::ABIModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(record);
        bare.types.push_back(length);
        bare.fields.push_back({record.id, "len", length.id, 0, 0});
        bare.functions.push_back(function);
        std::vector<uint8_t> bare_region;
        check(skl::amc::build_metadata_region(bare, options, bare_region, error), "build bare region");
        check(skl::amc::verify_abi(bare_region, expected, report, error)
                  && report.platform == skl::amc::TargetVerificationStatus::UNVERIFIABLE,
            "ABI verification: missing target is unverifiable");

        // A function without a calling convention: unverifiable, not compatible.
        skl::amc::ABIModule no_cc = m;
        no_cc.functions[0].calling_convention = 0;
        std::vector<uint8_t> no_cc_region;
        check(skl::amc::build_metadata_region(no_cc, options, no_cc_region, error), "build no-cc region");
        check(skl::amc::verify_abi(no_cc_region, expected, report, error)
                  && report.calling_convention == skl::amc::TargetVerificationStatus::UNVERIFIABLE,
            "ABI verification: missing calling convention is unverifiable");
    }

    {
        // Non-intrusive interface contract: interface identity, method identity
        // and implementation binding round-trip through .abix and the metadata
        // region without exposing any language's private vtable.
        auto make_primitive = [](const std::string &name, uint32_t size) {
            skl::amc::Type type;
            type.name = name;
            type.kind = skl::amc::TypeKind::PRIMITIVE;
            type.id = skl::amc::hash_text(name, 0x54595045);
            type.size = size;
            type.align = size;
            type.primitive_abi = skl::amc::primitive_abi(
                size * 8, skl::amc::PrimitiveABIKind::AMC_SINT, skl::amc::FloatFormat::NONE, true);
            return type;
        };
        skl::amc::ABIModule m;
        m.package_name = "iface";
        m.package_version = "1";
        m.pointer_bits = 64;
        m.types.push_back(make_primitive("int32_t", 4));
        skl::amc::Type renderer = make_type("Renderer", 8, 8);
        m.types.push_back(renderer);
        const skl::amc::Hash128 i32 = m.types[0].id;
        auto make_function = [&](const std::string &name, skl::amc::Hash128 owner) {
            skl::amc::Function function;
            function.name = name;
            function.owner_type = owner;
            function.return_type = i32;
            function.params = {
                { "width", i32, 0},
                {"height", i32, 0}
            };
            function.signature = skl::amc::signature_hash(function);
            return function;
        };
        m.functions.push_back(make_function("draw", skl::amc::Hash128{}));
        m.functions.push_back(make_function("resize", skl::amc::Hash128{}));
        m.functions.push_back(make_function("Renderer_draw", renderer.id));
        m.functions.push_back(make_function("Renderer_resize", renderer.id));

        skl::amc::InterfaceRecord iface;
        iface.name = "IRenderer";
        iface.version = 1;
        iface.method_begin = 0;
        iface.method_count = 2;
        for (uint32_t i = 0; i < 2; ++i) {
            skl::amc::InterfaceMethod method;
            method.name = m.functions[i].name;
            method.signature = m.functions[i].signature;
            method.return_type = m.functions[i].return_type;
            method.parameters = m.functions[i].params;
            method.slot = i;
            m.interface_methods.push_back(method);
        }
        iface.id = skl::amc::interface_hash(iface, m.interface_methods);
        for (auto &method : m.interface_methods)
            method.interface_id = iface.id;
        m.interfaces.push_back(iface);

        skl::amc::InterfaceImplementation impl;
        impl.interface_id = iface.id;
        impl.concrete_type = renderer.id;
        impl.binding_begin = 0;
        impl.binding_count = 2;
        for (uint32_t i = 0; i < 2; ++i) {
            skl::amc::MethodBinding binding;
            binding.interface_id = iface.id;
            binding.slot = m.interface_methods[i].slot;
            binding.method_signature = m.interface_methods[i].signature;
            binding.native_function = m.functions[2 + i].signature;
            m.method_bindings.push_back(binding);
        }
        m.implementations.push_back(impl);

        std::string error;
        check(skl::amc::validate(m, error), "interface module validates");
        // Identity depends on name+version+signatures, so a rename is a new
        // interface even though the shape is identical.
        skl::amc::InterfaceRecord renamed = iface;
        renamed.name = "IOther";
        check(!(skl::amc::interface_hash(renamed, m.interface_methods) == iface.id),
            "interface identity includes the name");
        skl::amc::InterfaceRecord bumped = iface;
        bumped.version = 2;
        check(!(skl::amc::interface_hash(bumped, m.interface_methods) == iface.id),
            "interface identity includes the version");

        check(skl::amc::write_abix(m, "amc_iface_test.abix", error), "write_abix interface");
        skl::amc::ABIModule loaded;
        check(skl::amc::read_abix("amc_iface_test.abix", loaded, error), "read_abix interface");
        check(loaded.interfaces.size() == 1
                  && loaded.interfaces[0].id == iface.id
                  && loaded.interface_methods.size() == 2
                  && loaded.implementations.size() == 1
                  && loaded.method_bindings.size() == 2
                  && loaded.method_bindings[0].native_function == m.functions[2].signature,
            "interface round-trips through .abix");

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        check(skl::amc::build_metadata_region(m, options, region, error), "build region with an interface");
        std::vector<skl::amc::MetadataInterfaceContract> contracts;
        std::vector<skl::amc::MetadataInterfaceMethod> methods;
        std::vector<skl::amc::MetadataInterfaceImplementation> impls;
        std::vector<skl::amc::MetadataMethodBinding> bindings;
        check(skl::amc::read_interface_contracts(region, contracts, error)
                  && contracts.size() == 1
                  && contracts[0].id == iface.id
                  && contracts[0].method_count == 2,
            "interface identity round-trips through the region");
        check(skl::amc::read_interface_methods(region, methods, error)
                  && methods.size() == 2
                  && methods[0].parameter_types.size() == 2
                  && methods[0].return_type == i32,
            "interface methods round-trip through the region");
        check(skl::amc::read_interface_implementations(region, impls, error)
                  && impls.size() == 1
                  && impls[0].concrete_type == renderer.id,
            "interface implementation round-trips through the region");
        check(skl::amc::read_method_bindings(region, bindings, error)
                  && bindings.size() == 2
                  && bindings[0].native_function == m.functions[2].signature,
            "method bindings round-trip through the region");

        // The ABIX runtime helper derives exactly the same interface identity,
        // so a hand-written consumer and AMC agree without sharing code. This is
        // the cross-tool contract for the canonical hash: both must reproduce
        // skl::amc::hash_text over the same canonical byte string. The list below is
        // deliberately out of slot order to exercise the runtime sort.
        {
            const skl::abix::poly::InterfaceMethodHash sig0{
                m.interface_methods[0].signature.lo, m.interface_methods[0].signature.hi};
            const skl::abix::poly::InterfaceMethodHash sig1{
                m.interface_methods[1].signature.lo, m.interface_methods[1].signature.hi};
            const skl::mics::util::array keys = {
                skl::abix::poly::InterfaceMethodKey{sig1, 1},
                skl::abix::poly::InterfaceMethodKey{sig0, 0},
            };
            const skl::abix::poly::InterfaceId by_slot = skl::abix::poly::interface_id("IRenderer", 1, keys);
            const skl::mics::util::array ordered = {sig0, sig1};
            const skl::abix::poly::InterfaceId by_signatures = skl::abix::poly::interface_id("IRenderer", 1, ordered);
            const skl::abix::poly::InterfaceId by_variadic = skl::abix::poly::interface_id("IRenderer", 1, sig0, sig1);
            check(by_slot.lo == iface.id.lo
                      && by_slot.hi == iface.id.hi
                      && by_signatures.lo == iface.id.lo
                      && by_signatures.hi == iface.id.hi
                      && by_variadic.lo == iface.id.lo
                      && by_variadic.hi == iface.id.hi,
                "ABIX interface_id matches AMC interface_hash");
        }

        // Fixed vectors pin the canonical hash constants (seed, primes, lane
        // mixing) and the identity input format. They were computed
        // independently of this implementation; if either AMC or ABIX changes a
        // constant, these fail instead of the two sides drifting silently.
        {
            const skl::amc::Hash128 empty = skl::amc::hash_text("", 0);
            check(empty.lo == 0xCBF29CE484222325ULL && empty.hi == 0x00000100000001B3ULL, "canonical hash seed vector");
            const skl::amc::Hash128 hello = skl::amc::hash_text("hello", 0x494E544552464143ULL);
            check(
                hello.lo == 0xC9CDDAA0A1F90676ULL && hello.hi == 0x277729AABE607CC7ULL, "canonical hash hello vector");

            const std::vector<skl::amc::Hash128> fixed_signatures = {
                {0x1111111111111111ULL, 0x2222222222222222ULL},
                {0x3333333333333333ULL, 0x4444444444444444ULL},
            };
            const skl::amc::Hash128 amc_fixed = skl::amc::interface_identity("IRenderer", 1, fixed_signatures);
            check(amc_fixed.lo == 0xB25E9B1A9FDB63AFULL && amc_fixed.hi == 0xB903D3C743AFC513ULL,
                "interface identity fixed vector (AMC)");
            const skl::mics::util::array abix_fixed = {
                skl::abix::poly::InterfaceMethodHash{0x1111111111111111ULL, 0x2222222222222222ULL},
                skl::abix::poly::InterfaceMethodHash{0x3333333333333333ULL, 0x4444444444444444ULL},
            };
            const skl::abix::poly::InterfaceId abix_fixed_id =
                skl::abix::poly::interface_id("IRenderer", 1, abix_fixed);
            check(abix_fixed_id.lo == 0xB25E9B1A9FDB63AFULL && abix_fixed_id.hi == 0xB903D3C743AFC513ULL,
                "interface identity fixed vector (ABIX)");

            // `canonical_type_id` and `method_signature` reproduce AMC's
            // name-based TypeID and `signature_hash` (the [[interface]] path).
            // AMC hashes the parameter TypeIDs but not the parameter names.
            const skl::abix::model::TypeId i32_type = skl::abix::model::canonical_type_id("prim:i32");
            const skl::abix::model::TypeId void_type = skl::abix::model::canonical_type_id("prim:void");
            const skl::amc::Hash128 amc_i32 = skl::amc::hash_text("prim:i32", 0x54595045);
            check(i32_type.lo == amc_i32.lo && i32_type.hi == amc_i32.hi, "canonical_type_id matches AMC");
            const skl::abix::poly::InterfaceMethodHash area_sig =
                skl::abix::poly::method_signature("area", i32_type, nullptr, 0);
            check(area_sig.lo == 0x5EFD5F6CF43E3493ULL && area_sig.hi == 0x374FF9C63FC19092ULL,
                "method signature no-parameter fixed vector");

            const skl::abix::model::TypeId resize_parameters[] = {i32_type, i32_type};
            const skl::abix::poly::InterfaceMethodHash resize_sig =
                skl::abix::poly::method_signature("resize", void_type, resize_parameters, 2);
            skl::amc::Function resize_function;
            resize_function.name = "resize";
            resize_function.return_type = skl::amc::hash_text("prim:void", 0x54595045);
            resize_function.params = {
                { "width", skl::amc::hash_text("prim:i32", 0x54595045), 0},
                {"height", skl::amc::hash_text("prim:i32", 0x54595045), 0}
            };
            const skl::amc::Hash128 amc_resize = skl::amc::signature_hash(resize_function);
            check(resize_sig.lo == amc_resize.lo && resize_sig.hi == amc_resize.hi,
                "ABIX method_signature matches AMC signature_hash");
            check(resize_sig.lo == 0x505E76C01A417017ULL && resize_sig.hi == 0x45A2B8E20BE7E96EULL,
                "method signature fixed vector");
        }

        // Canonical Layout Encoding v1: AMC and ABIX must produce identical
        // bytes and hashes for the same target ABI and layout facts.
        {
            skl::amc::ABIModule layout_module;
            layout_module.arch = 1;
            layout_module.os = 1;
            layout_module.target_abi = 1;
            layout_module.compiler = 1;
            skl::amc::Type point;
            point.name = "Point";
            point.kind = skl::amc::TypeKind::RECORD;
            point.id = skl::amc::hash_text("Point", 0x54595045);
            point.size = 8;
            point.align = 4;
            point.field_count = 2;
            const skl::amc::Hash128 nested_id = skl::amc::hash_text("Nested", 0x54595045);
            std::vector<skl::amc::Field> amc_fields = {
                {point.id, "x", skl::amc::hash_text("prim:i32", 0x54595045), 0, 0},
                {point.id, "y", nested_id, 4, (1u << 0) | (2u << 8) | (3u << 16) | (5u << 24)},
            };
            const std::vector<uint8_t> amc_bytes =
                skl::amc::canonical_layout_encoding(layout_module, point, amc_fields);
            const skl::amc::Hash128 amc_layout = skl::amc::canonical_layout_hash(layout_module, point, amc_fields);

            const skl::abix::model::LayoutContext context{
                layout_module.arch, layout_module.os, layout_module.target_abi, layout_module.compiler};
            const skl::abix::model::TypeId point_id{point.id.lo, point.id.hi};
            const skl::abix::model::TypeLayout layout{
                point.size, point.align, 0, point.field_count, {0, 0}
            };
            const skl::abix::model::Field abix_fields[] = {
                {0, {amc_fields[0].type_id.lo, amc_fields[0].type_id.hi}, amc_fields[0].offset,                 amc_fields[0].flags, 0,
                 0                                                                                                                       },
                // Visibility is deliberately different: CLE masks it out.
                {0,                         {nested_id.lo, nested_id.hi}, amc_fields[1].offset, (1u << 0) | (3u << 16) | (5u << 24), 0, 0},
            };
            uint8_t buffer[256] = {};
            const size_t encoded = skl::abix::model::encode_canonical_layout(
                context, point_id, layout, abix_fields, buffer, sizeof(buffer));
            check(encoded == amc_bytes.size() && std::equal(amc_bytes.begin(), amc_bytes.end(), buffer),
                "CLE bytes match between AMC and ABIX");
            const auto abix_layout = skl::abix::model::canonical_layout_hash(context, point_id, layout, abix_fields);
            check(abix_layout.lo == amc_layout.lo && abix_layout.hi == amc_layout.hi,
                "CLE hash matches between AMC and ABIX");
        }

        // `.abix` CLE v1: the optional selector is written, the reader
        // recomputes the canonical layout hash, and a tampered hash is rejected.
        {
            skl::amc::ABIModule layout_module;
            layout_module.package_name = "layout";
            layout_module.package_version = "1";
            layout_module.arch = 1;
            layout_module.os = 1;
            layout_module.target_abi = 1;
            layout_module.compiler = 1;
            skl::amc::Type i32_type;
            i32_type.name = "int32_t";
            i32_type.kind = skl::amc::TypeKind::PRIMITIVE;
            i32_type.id = skl::amc::hash_text("prim:i32", 0x54595045);
            i32_type.size = 4;
            i32_type.align = 4;
            layout_module.types.push_back(i32_type);
            skl::amc::Type record;
            record.name = "Point";
            record.kind = skl::amc::TypeKind::RECORD;
            record.id = skl::amc::hash_text("Point", 0x54595045);
            record.size = 8;
            record.align = 4;
            record.field_begin = 0;
            record.field_count = 2;
            layout_module.types.push_back(record);
            layout_module.fields.push_back({record.id, "x", i32_type.id, 0, 0});
            layout_module.fields.push_back({record.id, "y", i32_type.id, 4, 0});
            skl::amc::recompute_layout_hashes(layout_module);
            check(layout_module.layout_encoding_version == skl::amc::canonical_layout_version,
                "recompute marks the artifact as CLE v1");
            check(layout_module.types[1].layout_hash
                      == skl::amc::canonical_layout_hash(layout_module, layout_module.types[1], layout_module.fields),
                "recompute uses the canonical layout hash");
            check(skl::amc::write_abix(layout_module, "amc_layout_v1.abix", error), "write CLE v1 artifact");
            skl::amc::ABIModule loaded_layout;
            check(skl::amc::read_abix("amc_layout_v1.abix", loaded_layout, error), "read CLE v1 artifact");
            check(loaded_layout.layout_encoding_version == skl::amc::canonical_layout_version
                      && loaded_layout.types[1].layout_hash == layout_module.types[1].layout_hash,
                "CLE v1 selector and hash round-trip");

            std::vector<uint8_t> layout_region;
            check(skl::amc::build_metadata_region(layout_module, skl::amc::MetadataOptions{}, layout_region, error),
                "build metadata region with a CLE version");
            uint32_t region_layout_version = 0;
            bool region_layout_present = false;
            check(skl::amc::read_layout_encoding(layout_region, region_layout_version, region_layout_present, error)
                      && region_layout_present
                      && region_layout_version == skl::amc::canonical_layout_version,
                "metadata region records the CLE version");

            // Tamper the on-disk type table's layout hash; the reader must
            // recompute CLE and reject the mismatch.
            {
                std::fstream file("amc_layout_v1.abix", std::ios::binary | std::ios::in | std::ios::out);
                const std::streamoff directory = 20 + 3 * 24;   // section index 3 is `types`
                file.seekg(directory + 4);
                uint8_t raw[4] = {};
                file.read(reinterpret_cast<char *>(raw), 4);
                const uint32_t type_offset =
                    uint32_t(raw[0]) | (uint32_t(raw[1]) << 8) | (uint32_t(raw[2]) << 16) | (uint32_t(raw[3]) << 24);
                file.seekp(static_cast<std::streamoff>(type_offset) + 16);   // skip the TypeID
                const char flipped = 1;
                file.write(&flipped, 1);
            }
            skl::amc::ABIModule rejected;
            check(!skl::amc::read_abix("amc_layout_v1.abix", rejected, error), "tampered CLE hash is rejected");
        }

        // A region with no interface reads empty.
        skl::amc::ABIModule bare;
        bare.package_name = "bare";
        bare.package_version = "1";
        bare.types.push_back(m.types[0]);
        std::vector<uint8_t> bare_region;
        check(skl::amc::build_metadata_region(bare, options, bare_region, error), "build bare interface region");
        std::vector<skl::amc::MetadataInterfaceContract> none;
        check(skl::amc::read_interface_contracts(bare_region, none, error) && none.empty(),
            "no interface means an empty region read");

        // A signature mismatch between a binding and its interface method is
        // rejected rather than tolerated.
        skl::amc::ABIModule bad = m;
        bad.method_bindings[1].method_signature = m.functions[2].signature;
        check(!skl::amc::validate(bad, error), "binding signature mismatch is rejected");

        // An implementation that does not bind every method is rejected.
        skl::amc::ABIModule partial = m;
        partial.implementations[0].binding_count = 1;
        check(!skl::amc::validate(partial, error), "incomplete implementation is rejected");

        // Projection preserves the contract when its types survive.
        skl::amc::ABIModule projected_all;
        check(skl::amc::project_symbols(m, {}, projected_all, error)
                  && projected_all.interfaces.size() == 1
                  && projected_all.interface_methods.size() == 2
                  && projected_all.method_bindings.size() == 2,
            "empty projection keeps the interface contract");
        std::remove("amc_iface_test.abix");
    }

    {
        // `void` is a first-class primitive with no size; a function may return
        // it. The size/align requirement is skipped for it, and it survives both
        // serialization paths.
        skl::amc::ABIModule m;
        m.package_name = "void_test";
        m.package_version = "1";
        skl::amc::Type void_type;
        void_type.name = "void";
        void_type.kind = skl::amc::TypeKind::PRIMITIVE;
        void_type.id = skl::amc::hash_text("void", 0x54595045);
        void_type.size = 0;
        void_type.align = 1;
        void_type.primitive_abi = skl::amc::primitive_abi(0, skl::amc::PrimitiveABIKind::AMC_VOID);
        m.types.push_back(void_type);
        skl::amc::Function ping;
        ping.name = "ping";
        ping.return_type = void_type.id;
        ping.signature = skl::amc::signature_hash(ping);
        m.functions.push_back(ping);

        std::string error;
        check(skl::amc::validate(m, error), "a void return type validates");
        check(skl::amc::write_abix(m, "amc_void_test.abix", error), "write a void module");
        skl::amc::ABIModule loaded;
        check(skl::amc::read_abix("amc_void_test.abix", loaded, error), "read a void module");
        check(loaded.types.size() == 1
                  && loaded.functions.size() == 1
                  && loaded.types[0].size == 0
                  && loaded.functions[0].return_type == void_type.id,
            "void round-trips through .abix");

        skl::amc::MetadataOptions options;
        std::vector<uint8_t> region;
        check(skl::amc::build_metadata_region(m, options, region, error), "build a region with void");
        skl::amc::ABIModule from_region;
        check(skl::amc::module_from_region(region, from_region, error)
                  && from_region.types.size() == 1
                  && from_region.types[0].size == 0,
            "void round-trips through the metadata region");
        std::remove("amc_void_test.abix");
    }

    {
        // M3.4: instantiation request cache. Identity is layered, and a build
        // key covers every input that can change the reusable product.
        auto make_request = [](const std::string &tmpl, std::vector<std::string> args) {
            skl::amc::InstantiationRequest request;
            request.template_name = tmpl;
            request.arguments = std::move(args);
            request.arch = 1;
            request.os = 1;
            request.target_abi = 1;
            request.pointer_bits = 64;
            return request;
        };
        auto make_inputs = [&](const skl::amc::InstantiationRequest &request) {
            skl::amc::InstantiationBuildInputs inputs;
            inputs.request = request;
            inputs.source_digest = skl::amc::digest_text("source-v1", skl::amc::digest_domain_source);
            inputs.toolchain = skl::amc::digest_text("clang", skl::amc::digest_domain_toolchain);
            inputs.build_options = skl::amc::digest_text("-O2", skl::amc::digest_domain_options);
            return inputs;
        };

        const auto request = make_request("abix_span", {"int32_t"});
        const auto same_shape = make_request("abix_span", {"int32_t"});
        const auto reordered = make_request("abix_span", {"float", "int32_t"});
        const auto other_template = make_request("abix_array", {"int32_t"});
        auto retargeted = request;
        retargeted.pointer_bits = 32;
        auto qualified = make_request("abix_span", {"const int32_t *"});

        check(skl::amc::instantiation_request_id(request) == skl::amc::instantiation_request_id(same_shape),
            "same request has one identity");
        check(!(skl::amc::instantiation_request_id(request) == skl::amc::instantiation_request_id(other_template))
                  && !(skl::amc::instantiation_request_id(request) == skl::amc::instantiation_request_id(reordered)),
            "template or argument order changes the request identity");
        check(!(skl::amc::instantiation_request_id(request) == skl::amc::instantiation_request_id(retargeted)),
            "target changes the request identity");
        // A spelling that canonicalises to the same argument is one request.
        check(skl::amc::instantiation_request_id(request) == skl::amc::instantiation_request_id(qualified),
            "equivalent normalised arguments share the request identity");
        // Same template, different target: same template signature, distinct request.
        check(skl::amc::instantiation_template_signature(request)
                      == skl::amc::instantiation_template_signature(retargeted)
                  && !(skl::amc::instantiation_request_id(request) == skl::amc::instantiation_request_id(retargeted)),
            "template signature is target-agnostic");

        // Build key covers every reuse-decision input.
        const auto base = make_inputs(request);
        check(skl::amc::instantiation_build_key(base) == skl::amc::instantiation_build_key(make_inputs(same_shape)),
            "identical build inputs share a build key");
        auto source_changed = base;
        source_changed.source_digest = skl::amc::digest_text("source-v2", skl::amc::digest_domain_source);
        auto toolchain_changed = base;
        toolchain_changed.toolchain = skl::amc::digest_text("clang-20", skl::amc::digest_domain_toolchain);
        auto options_changed = base;
        options_changed.build_options = skl::amc::digest_text("-O3", skl::amc::digest_domain_options);
        auto protocol_changed = base;
        protocol_changed.protocol_version = 2;
        auto schema_changed = base;
        schema_changed.schema_version = 2;
        check(!(skl::amc::instantiation_build_key(base) == skl::amc::instantiation_build_key(source_changed))
                  && !(skl::amc::instantiation_build_key(base) == skl::amc::instantiation_build_key(toolchain_changed))
                  && !(skl::amc::instantiation_build_key(base) == skl::amc::instantiation_build_key(options_changed))
                  && !(skl::amc::instantiation_build_key(base) == skl::amc::instantiation_build_key(protocol_changed))
                  && !(skl::amc::instantiation_build_key(base) == skl::amc::instantiation_build_key(schema_changed)),
            "build key changes with source, toolchain, options, protocol or schema");

        // A scratch cache directory.
        const auto root =
            std::filesystem::temp_directory_path()
            / ("amc_cache_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        skl::amc::InstantiationCache cache(root.string());
        std::string error;
        const std::vector<std::pair<std::string, std::string>> artifacts = {
            {"module.abix", "ARTIFACT-A"}
        };

        const auto empty = cache.lookup(base, skl::amc::LookupOptions{true});
        check(empty.status == skl::amc::CacheStatus::MISS
                  && empty.reason == std::string(skl::amc::cache_reason_not_found),
            "empty cache misses with not_found");
        check(cache.store(base, artifacts, error), "store an instantiation product");
        const auto hit = cache.lookup(base);
        check(hit.status == skl::amc::CacheStatus::HIT
                  && hit.reason == std::string(skl::amc::cache_reason_hit)
                  && hit.artifacts.size() == 1
                  && hit.artifacts[0].name == "module.abix",
            "repeated build hits the cache");
        check(cache.verify(hit.build_key, error), "committed entry verifies");
        check(cache.entries() == 1, "one committed entry");

        // Only export alias changes: not part of request identity, so it is the
        // same request and hits. (The alias lives in the exported symbol layer.)
        check(cache.lookup(make_inputs(make_request("abix_span", {"int32_t"}))).status == skl::amc::CacheStatus::HIT,
            "an alias-only change is not a new instantiation request");

        // Each input change yields a miss with a precise reason.
        const auto fast_miss = cache.lookup(source_changed);
        check(fast_miss.status == skl::amc::CacheStatus::MISS
                  && fast_miss.reason == std::string(skl::amc::cache_reason_not_found),
            "fast lookup skips the diagnostic scan");
        auto reason_for = [&](const skl::amc::InstantiationBuildInputs &inputs) {
            const auto result = cache.lookup(inputs, skl::amc::LookupOptions{true});
            return result.status == skl::amc::CacheStatus::MISS ? result.reason : std::string();
        };
        check(reason_for(source_changed) == std::string(skl::amc::cache_reason_source_digest_changed),
            "dependency change invalidates with source_digest_changed");
        check(reason_for(toolchain_changed) == std::string(skl::amc::cache_reason_toolchain_changed),
            "toolchain change invalidates with toolchain_changed");
        check(reason_for(options_changed) == std::string(skl::amc::cache_reason_build_options_changed),
            "build option change invalidates with build_options_changed");
        check(reason_for(make_inputs(retargeted)) == std::string(skl::amc::cache_reason_target_changed),
            "target change invalidates with target_changed");
        check(reason_for(protocol_changed) == std::string(skl::amc::cache_reason_protocol_version_changed),
            "protocol change invalidates with protocol_version_changed");
        check(reason_for(schema_changed) == std::string(skl::amc::cache_reason_schema_version_changed),
            "schema change invalidates with schema_version_changed");

        // A corrupted artifact is rejected, then repaired by the next store.
        const auto entry = std::filesystem::path(hit.entry);
        {
            std::string blob;
            check(read_binary(entry, blob) && blob.size() > 1, "a committed entry is a single readable file");
            blob.back() = static_cast<char>(blob.back() ^ 0x01);
            check(write_binary(entry, blob), "a committed entry can be tampered with");
        }
        check(cache.lookup(base).status == skl::amc::CacheStatus::CORRUPTED
                  && cache.lookup(base).reason == std::string(skl::amc::cache_reason_corrupted_artifact),
            "a corrupted artifact is rejected");
        check(!cache.verify(hit.build_key, error), "corrupted entry fails verification");
        check(cache.store(base, artifacts, error) && cache.lookup(base).status == skl::amc::CacheStatus::HIT,
            "a corrupt entry is replaced by a re-store");

        // A malformed manifest is reported rather than trusted.
        {
            std::string blob;
            const bool readable = read_binary(entry, blob);
            const size_t manifest_offset = cache_entry_manifest_offset(blob);
            check(readable && manifest_offset + 10 <= blob.size(), "a committed entry has a manifest region");
            if (manifest_offset + 10 <= blob.size()) {
                blob.replace(manifest_offset, 10, "{ not json");
                check(write_binary(entry, blob), "a committed entry manifest can be tampered with");
            }
        }
        check(cache.lookup(base).status == skl::amc::CacheStatus::INVALID_MANIFEST, "a malformed manifest is reported");
        check(cache.store(base, artifacts, error), "re-store replaces a malformed manifest");

        // Unsafe artifact names never touch the filesystem.
        check(!cache.store(base,
                  {
                      {"../escape", "x"}
        },
                  error),
            "an unsafe artifact name is refused");

        // Stats and pruning.
        cache.record_lookup(skl::amc::CacheStatus::HIT);
        cache.record_lookup(skl::amc::CacheStatus::MISS);
        cache.record_store();
        const auto stats = cache.stats();
        check(stats.entries == 1 && stats.bytes > 0, "stats reports the committed entry");
        check(stats.hits == 1 && stats.misses == 1 && stats.stores == 1, "stats records lookup and store counters");
        check(cache.prune(skl::amc::PruneOptions{0, 0}).removed == 0, "prune without limits keeps every entry");
        const auto dry = cache.prune(skl::amc::PruneOptions{1, 0}, true);
        check(dry.removed == 1 && dry.freed_bytes == stats.bytes, "prune dry-run reports what would be removed");
        check(cache.entries() == 1, "prune dry-run removes nothing");

        check(cache.clear() >= 1 && cache.entries() == 0, "cache can be cleared");
        check(cache.store(base, artifacts, error), "re-store after clear");
        const auto pruned = cache.prune(skl::amc::PruneOptions{1, 0});
        check(pruned.removed == 1 && pruned.freed_bytes > 0 && cache.entries() == 0, "prune honours the byte budget");
        cleanup_dir(root);

        // Concurrent writers of a fresh key must leave one complete, verifiable
        // entry, never a partial or mixed product.
        {
            const auto concurrent_root = std::filesystem::path(root.string() + "_concurrent");
            skl::amc::InstantiationCache concurrent(concurrent_root.string());
            std::string local_error;
            std::vector<std::thread> writers;
            for (int i = 0; i < 8; ++i) {
                writers.emplace_back([&, i] {
                    const std::string content = "WRITER-" + std::to_string(i);
                    std::string thread_error;
                    skl::amc::InstantiationCache per_thread(concurrent_root.string());
                    (void)per_thread.store(base,
                        {
                            {"module.abix", content}
                    },
                        thread_error);
                });
            }
            for (auto &writer : writers)
                writer.join();
            check(concurrent.verify(skl::amc::instantiation_build_key(base), local_error),
                "concurrent writers leave a verifiable entry");
            const auto after = concurrent.lookup(base);
            check(
                after.status == skl::amc::CacheStatus::HIT && after.artifacts.size() == 1 && concurrent.entries() == 1,
                "concurrent writers leave one consistent entry");
            cleanup_dir(concurrent_root);
        }

        // A recorded dependency (the frontend's include closure) invalidates the
        // entry when it changes, without the cache key listing it.
        {
            const auto dep_root = std::filesystem::path(root.string() + "_deps");
            std::filesystem::create_directories(dep_root);
            const auto dep_file = dep_root / "dep.hpp";
            { std::ofstream(dep_file) << "v1"; }
            skl::amc::InstantiationCache dependency_cache(dep_root.string());
            const auto dependency_for = [&](const std::string &content) {
                return std::vector<skl::amc::CacheDependency>{
                    {dep_file.string(),
                     skl::amc::digest_bytes(content.data(), content.size(), skl::amc::digest_domain_dependency)}
                };
            };
            std::string dependency_error;
            check(dependency_cache.store(base,
                      {
                          {"module.abix", "C"}
            },
                      dependency_for("v1"), dependency_error),
                "store with a dependency closure");
            check(dependency_cache.lookup(base).status == skl::amc::CacheStatus::HIT, "unchanged dependency hits");
            { std::ofstream(dep_file, std::ios::app) << "more"; }
            check(dependency_cache.lookup(base).status == skl::amc::CacheStatus::MISS
                      && dependency_cache.lookup(base).reason == std::string(skl::amc::cache_reason_dependency_changed),
                "a changed dependency invalidates the entry");
            check(dependency_cache.store(base,
                      {
                          {"module.abix", "C2"}
            },
                      dependency_for("v1more"), dependency_error)
                      && dependency_cache.lookup(base).status == skl::amc::CacheStatus::HIT,
                "re-store refreshes the dependency closure");
            cleanup_dir(dep_root);
        }

#if !defined(_WIN32)
        // Cross-process writers of a fresh key: temporary directories must be
        // process-unique, and the committed entry must remain one complete,
        // verifiable product rather than a mix.
        {
            const auto process_root = std::filesystem::path(root.string() + "_proc");
            std::vector<pid_t> children;
            for (int i = 0; i < 4; ++i) {
                const pid_t pid = fork();
                if (pid == 0) {
                    skl::amc::InstantiationCache child_cache(process_root.string());
                    std::string child_error;
                    const std::string content = "PROC-" + std::to_string(i);
                    (void)child_cache.store(base,
                        {
                            {"module.abix", content}
                    },
                        child_error);
                    _exit(0);
                }
                if (pid > 0) children.push_back(pid);
            }
            for (const pid_t pid : children) {
                int status = 0;
                waitpid(pid, &status, 0);
                check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "cache child exits cleanly");
            }
            skl::amc::InstantiationCache process_cache(process_root.string());
            check(process_cache.entries() == 1, "cross-process writers leave one entry");
            check(process_cache.verify(skl::amc::instantiation_build_key(base), error), "cross-process entry verifies");
            const auto process_hit = process_cache.lookup(base);
            check(process_hit.status == skl::amc::CacheStatus::HIT && process_hit.artifacts.size() == 1,
                "cross-process entry is a consistent hit");
            cleanup_dir(process_root);
        }
#endif
    }

    if (failures != 0) {
        fmt::print(stderr, "amc-core-test: {} failure(s)\n", failures);
        return 1;
    }
    fmt::print("amc-core-test: all checks passed\n");
    return 0;
}
