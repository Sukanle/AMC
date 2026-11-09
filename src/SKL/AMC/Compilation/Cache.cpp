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
#include "SKL/AMC/Compilation/Cache.h"

#include "SKL/AMC/Compilation/Reflect.h"
#include "SKL/AMC/Support/Process.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>

#include "llvm/Support/MemoryBuffer.h"

namespace skl::amc {
namespace {

namespace fs = std::filesystem;

constexpr const char *kEntrySuffix = ".entry";
constexpr const char *kStatsName = "stats";
// One entry is a single file:
//
//   magic "AMCACHv2" | u32 format | u32 flags | u32 manifest_size |
//   u32 artifact_count | u64 artifact_size[count] | manifest | payload...
//
// The manifest stays the line-oriented format it always was; the size table sits
// in front of it so a lookup can read the header and manifest without touching
// the payload. Integers are little-endian on disk.
constexpr const char *kEntryMagic = "AMCACHv2";
constexpr uint32_t kEntryFormatVersion = 2;
constexpr uint64_t kEntryHeaderSize = 8 + 4 + 4 + 4 + 4;
constexpr uint32_t kEntryMaxArtifacts = 4'096;
// Entries larger than this are mapped instead of read into a temporary buffer.
// Mapping a small entry costs more than reading it (the mapping code stats the
// file and faults a page), but it avoids a full copy once the payload is large.
constexpr uint64_t kMapThreshold = 64 * 1'024;
constexpr const char *kManifestSchema = "abix.instantiation-cache/1";
constexpr uint64_t kRequestDomain = 0x494e5354ULL;    // "INST"
constexpr uint64_t kTemplateDomain = 0x54454d50ULL;   // "TEMP"

std::string hex_u64(uint64_t value) { return fmt::format("{:016x}", value); }

std::string hash_hex_plain(Hash128 value) { return hex_u64(value.hi) + hex_u64(value.lo); }

bool parse_hex_u64(const std::string &text, size_t offset, uint64_t &value) {
    if (text.size() < offset + 16) return false;
    uint64_t result = 0;
    for (size_t i = 0; i < 16; ++i) {
        const char c = text[offset + i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else
            return false;
        result = (result << 4) | digit;
    }
    value = result;
    return true;
}

bool parse_hash_hex(const std::string &text, Hash128 &value) {
    if (text.size() != 32) return false;
    return parse_hex_u64(text, 0, value.hi) && parse_hex_u64(text, 16, value.lo);
}

// Mirror of Core's spelling normalisation: drop cv/elaborated qualifiers,
// whitespace and pointer/reference decoration so `const int *` and `int`-based
// spellings canonicalise consistently.
std::string normalise_spelling(const std::string &spelling) {
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

bool valid_artifact_name(const std::string &name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return true;
}

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

void put_u32(std::string &out, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xff));
}

void put_u64(std::string &out, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xff));
}

uint32_t get_u32(const char *data) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<uint32_t>(static_cast<unsigned char>(data[shift / 8])) << shift;
    return value;
}

uint64_t get_u64(const char *data) {
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<uint64_t>(static_cast<unsigned char>(data[shift / 8])) << shift;
    return value;
}

bool add_overflows(uint64_t a, uint64_t b, uint64_t &out) {
    if (a > UINT64_MAX - b) return true;
    out = a + b;
    return false;
}

uint64_t next_sequence() {
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

struct Manifest {
    uint32_t schema_version = instantiation_schema_version;
    uint32_t protocol_version = instantiation_protocol_version;
    Hash128 build_key{};
    InstantiationRequest request;
    Hash128 template_signature{};
    Hash128 request_id{};
    Hash128 source_digest{};
    Hash128 toolchain{};
    Hash128 build_options{};
    std::vector<CacheArtifact> artifacts;
    std::vector<CacheDependency> dependencies;
};

std::string encode_hex(const std::string &value) {
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2);
    for (const unsigned char c : value) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0xf]);
    }
    return out;
}

bool decode_hex(const std::string &value, std::string &out) {
    if (value.size() % 2 != 0) return false;
    out.clear();
    out.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = digit(value[i]);
        const int lo = digit(value[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

bool parse_u32_text(const std::string &text, uint32_t &value) {
    if (text.empty()) return false;
    uint64_t result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + static_cast<uint64_t>(c - '0');
        if (result > UINT32_MAX) return false;
    }
    value = static_cast<uint32_t>(result);
    return true;
}

// A deterministic, self-contained manifest. The bundled JSON printer/parser is
// not round-trip safe here, so the cache owns its format: one `key=value` per
// line, with free-form strings hex-encoded.
std::string manifest_text(const Manifest &manifest) {
    std::string out = kManifestSchema;
    out += "\n";
    out += "schema_version=" + std::to_string(manifest.schema_version) + "\n";
    out += "protocol_version=" + std::to_string(manifest.protocol_version) + "\n";
    out += "build_key=" + hash_hex_plain(manifest.build_key) + "\n";
    out += "source_digest=" + hash_hex_plain(manifest.source_digest) + "\n";
    out += "toolchain=" + hash_hex_plain(manifest.toolchain) + "\n";
    out += "build_options=" + hash_hex_plain(manifest.build_options) + "\n";
    out += "template=" + encode_hex(manifest.request.template_name) + "\n";
    for (const auto &argument : manifest.request.arguments)
        out += "arg=" + encode_hex(argument) + "\n";
    out += "arch=" + std::to_string(manifest.request.arch) + "\n";
    out += "os=" + std::to_string(manifest.request.os) + "\n";
    out += "target_abi=" + std::to_string(manifest.request.target_abi) + "\n";
    out += "pointer_bits=" + std::to_string(manifest.request.pointer_bits) + "\n";
    out += "contract=" + encode_hex(manifest.request.contract) + "\n";
    for (const auto &artifact : manifest.artifacts)
        out += "artifact=" + encode_hex(artifact.name) + ":" + hash_hex_plain(artifact.digest) + "\n";
    for (const auto &dependency : manifest.dependencies)
        out += "dependency=" + encode_hex(dependency.path) + ":" + hash_hex_plain(dependency.digest) + "\n";
    return out;
}

bool manifest_parse(const std::string &text, Manifest &manifest, std::string &error) {
    size_t line_begin = 0;
    bool first = true;
    bool have_template = false;
    while (line_begin <= text.size()) {
        const size_t line_end = text.find('\n', line_begin);
        const std::string line =
            text.substr(line_begin, line_end == std::string::npos ? std::string::npos : line_end - line_begin);
        if (first) {
            if (line != kManifestSchema) {
                error = "manifest schema is missing or unknown";
                return false;
            }
            first = false;
        } else if (!line.empty()) {
            const size_t separator = line.find('=');
            if (separator == std::string::npos) {
                error = "manifest line is malformed";
                return false;
            }
            const std::string key = line.substr(0, separator);
            const std::string value = line.substr(separator + 1);
            if (key == "schema_version") {
                if (!parse_u32_text(value, manifest.schema_version)) return false;
            } else if (key == "protocol_version") {
                if (!parse_u32_text(value, manifest.protocol_version)) return false;
            } else if (key == "build_key") {
                if (!parse_hash_hex(value, manifest.build_key)) return false;
            } else if (key == "source_digest") {
                if (!parse_hash_hex(value, manifest.source_digest)) return false;
            } else if (key == "toolchain") {
                if (!parse_hash_hex(value, manifest.toolchain)) return false;
            } else if (key == "build_options") {
                if (!parse_hash_hex(value, manifest.build_options)) return false;
            } else if (key == "template") {
                if (!decode_hex(value, manifest.request.template_name)) return false;
                have_template = true;
            } else if (key == "arg") {
                std::string argument;
                if (!decode_hex(value, argument)) return false;
                manifest.request.arguments.push_back(std::move(argument));
            } else if (key == "arch") {
                if (!parse_u32_text(value, manifest.request.arch)) return false;
            } else if (key == "os") {
                if (!parse_u32_text(value, manifest.request.os)) return false;
            } else if (key == "target_abi") {
                if (!parse_u32_text(value, manifest.request.target_abi)) return false;
            } else if (key == "pointer_bits") {
                if (!parse_u32_text(value, manifest.request.pointer_bits)) return false;
            } else if (key == "contract") {
                if (!decode_hex(value, manifest.request.contract)) return false;
            } else if (key == "artifact") {
                const size_t colon = value.find(':');
                if (colon == std::string::npos) {
                    error = "manifest artifact is malformed";
                    return false;
                }
                CacheArtifact artifact;
                if (!decode_hex(value.substr(0, colon), artifact.name)
                    || !valid_artifact_name(artifact.name)
                    || !parse_hash_hex(value.substr(colon + 1), artifact.digest)) {
                    error = "manifest artifact is malformed";
                    return false;
                }
                manifest.artifacts.push_back(std::move(artifact));
            } else if (key == "dependency") {
                const size_t colon = value.find(':');
                if (colon == std::string::npos) {
                    error = "manifest dependency is malformed";
                    return false;
                }
                CacheDependency dependency;
                if (!decode_hex(value.substr(0, colon), dependency.path)
                    || dependency.path.empty()
                    || !parse_hash_hex(value.substr(colon + 1), dependency.digest)) {
                    error = "manifest dependency is malformed";
                    return false;
                }
                manifest.dependencies.push_back(std::move(dependency));
            }
        }
        if (line_end == std::string::npos) break;
        line_begin = line_end + 1;
    }
    if (!have_template) {
        error = "manifest request is malformed";
        return false;
    }
    // Replay fields are derived, never trusted from disk.
    manifest.template_signature = instantiation_template_signature(manifest.request);
    manifest.request_id = instantiation_request_id(manifest.request);
    return true;
}

// A dependency is still reusable while its current content hashes to the
// recorded digest. A missing file counts as changed.
bool dependency_intact(const CacheDependency &dependency) {
    std::string content;
    if (!read_file(fs::path(dependency.path), content)) return false;
    return digest_bytes(content.data(), content.size(), digest_domain_dependency) == dependency.digest;
}

// Persisted usage counters. The file is a few short lines so it can be appended
// to without a parser; updates are read-modify-write and therefore approximate
// when several builds share one cache.
struct CacheCounters {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t stores = 0;
};

uint64_t parse_counter(const std::string &text) {
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return 0;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    return value;
}

CacheCounters read_counters(const fs::path &root) {
    CacheCounters counters;
    std::string text;
    if (!read_file(root / kStatsName, text)) return counters;
    size_t begin = 0;
    while (begin < text.size()) {
        const size_t end = text.find('\n', begin);
        const std::string line = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const size_t space = line.find(' ');
        if (space != std::string::npos) {
            const std::string key = line.substr(0, space);
            const uint64_t value = parse_counter(line.substr(space + 1));
            if (key == "hits")
                counters.hits = value;
            else if (key == "misses")
                counters.misses = value;
            else if (key == "stores")
                counters.stores = value;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return counters;
}

void write_counters(const fs::path &root, const CacheCounters &counters) {
    const std::string text = "hits "
                           + std::to_string(counters.hits)
                           + "\n"
                           + "misses "
                           + std::to_string(counters.misses)
                           + "\n"
                           + "stores "
                           + std::to_string(counters.stores)
                           + "\n";
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return;
    const fs::path path = root / kStatsName;
    const fs::path temporary = root / ".stats.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) {
            fs::remove(temporary, ec);
            return;
        }
    }
    fs::remove(path, ec);
    fs::rename(temporary, path, ec);
    if (ec) fs::remove(temporary, ec);
}

bool same_dependencies(const std::vector<CacheDependency> &a, const std::vector<CacheDependency> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].path != b[i].path || !(a[i].digest == b[i].digest)) return false;
    return true;
}

// A committed entry as read from disk: the manifest plus the payload layout.
// The payload is never read here, so a lookup touches the header and manifest
// only.
struct EntryFile {
    Manifest manifest;
    std::vector<uint64_t> sizes;
    uint64_t payload_offset = 0;
};

enum class EntryRead {
    OK,
    MISSING,     // no entry file for this key
    MALFORMED,   // an entry exists but its header/manifest is unusable
};

EntryRead read_entry_file(const fs::path &file, EntryFile &out, std::string &error) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "cache entry is missing";
        return EntryRead::MISSING;
    }
    char header[kEntryHeaderSize];
    if (!input.read(header, static_cast<std::streamsize>(kEntryHeaderSize))) {
        error = "cache entry is truncated";
        return EntryRead::MALFORMED;
    }
    if (memcmp(header, kEntryMagic, 8) != 0) {
        error = "cache entry magic is unknown";
        return EntryRead::MALFORMED;
    }
    const uint32_t format = get_u32(header + 8);
    const uint32_t manifest_size = get_u32(header + 16);
    const uint32_t artifact_count = get_u32(header + 20);
    if (format != kEntryFormatVersion) {
        error = "cache entry format is unsupported";
        return EntryRead::MALFORMED;
    }
    if (manifest_size == 0 || artifact_count > kEntryMaxArtifacts) {
        error = "cache entry header is malformed";
        return EntryRead::MALFORMED;
    }
    const uint64_t table_size = 8ull * artifact_count;
    uint64_t body_size = 0;
    if (add_overflows(table_size, manifest_size, body_size)) {
        error = "cache entry header is malformed";
        return EntryRead::MALFORMED;
    }
    std::string body(static_cast<size_t>(body_size), '\0');
    if (!input.read(body.data(), static_cast<std::streamsize>(body_size))) {
        error = "cache entry is truncated";
        return EntryRead::MALFORMED;
    }

    std::vector<uint64_t> sizes(artifact_count, 0);
    uint64_t total = 0;
    if (add_overflows(kEntryHeaderSize, body_size, total)) {
        error = "cache entry header is malformed";
        return EntryRead::MALFORMED;
    }
    const uint64_t payload_offset = total;
    for (uint32_t i = 0; i < artifact_count; ++i) {
        sizes[i] = get_u64(body.data() + 8ull * i);
        if (add_overflows(total, sizes[i], total)) {
            error = "cache entry header is malformed";
            return EntryRead::MALFORMED;
        }
    }

    Manifest manifest;
    if (!manifest_parse(body.substr(static_cast<size_t>(table_size)), manifest, error)) return EntryRead::MALFORMED;
    if (manifest.artifacts.size() != artifact_count) {
        error = "cache entry manifest does not match its size table";
        return EntryRead::MALFORMED;
    }
    std::error_code ec;
    const auto file_size = fs::file_size(file, ec);
    if (ec || static_cast<uint64_t>(file_size) != total) {
        error = "cache entry size does not match its header";
        return EntryRead::MALFORMED;
    }
    out.manifest = std::move(manifest);
    out.sizes = std::move(sizes);
    out.payload_offset = payload_offset;
    return EntryRead::OK;
}

uint64_t artifact_offset(const EntryFile &entry, size_t index) {
    uint64_t offset = entry.payload_offset;
    for (size_t i = 0; i < index; ++i)
        offset += entry.sizes[i];
    return offset;
}

// Mapping pays off once the payload is big enough to be worth avoiding the copy;
// below that the extra stat and page faults cost more than reading the bytes.
bool entry_prefers_mapping(const EntryFile &entry) {
    uint64_t total = entry.payload_offset;
    for (const auto size : entry.sizes)
        if (add_overflows(total, size, total)) return true;
    return total > kMapThreshold;
}

bool read_entry_payload(
    const fs::path &file, const EntryFile &entry, size_t index, std::string &out, std::string &error) {
    if (index >= entry.sizes.size()) {
        error = "cache entry artifact index is out of range";
        return false;
    }
    if (entry_prefers_mapping(entry)) {
        auto buffer = llvm::MemoryBuffer::getFile(file.string());
        if (!buffer) {
            error = "cache entry is missing";
            return false;
        }
        const llvm::StringRef data = (*buffer)->getBuffer();
        const uint64_t offset = artifact_offset(entry, index);
        const uint64_t size = entry.sizes[index];
        if (offset > data.size() || size > data.size() - offset) {
            error = "cache entry is truncated";
            return false;
        }
        out.assign(data.data() + offset, static_cast<size_t>(size));
        return true;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "cache entry is missing";
        return false;
    }
    input.seekg(static_cast<std::streamoff>(artifact_offset(entry, index)));
    const size_t size = static_cast<size_t>(entry.sizes[index]);
    out.assign(size, '\0');
    if (size != 0 && !input.read(out.data(), static_cast<std::streamsize>(size))) {
        error = "cache entry is truncated";
        return false;
    }
    return true;
}

// Verify one committed entry: every artifact payload hashes to the digest its
// manifest recorded. Large entries are mapped once, so verification does not
// reopen the file per artifact.
bool verify_entry_payload(const fs::path &file, const EntryFile &entry, std::string &error) {
    if (entry_prefers_mapping(entry)) {
        auto buffer = llvm::MemoryBuffer::getFile(file.string());
        if (!buffer) {
            error = "cache entry is missing";
            return false;
        }
        const llvm::StringRef data = (*buffer)->getBuffer();
        uint64_t offset = entry.payload_offset;
        for (size_t i = 0; i < entry.manifest.artifacts.size(); ++i) {
            const uint64_t size = entry.sizes[i];
            if (offset > data.size() || size > data.size() - offset) {
                error = "cache entry is truncated";
                return false;
            }
            const Hash128 digest =
                digest_bytes(data.data() + offset, static_cast<size_t>(size), digest_domain_artifact);
            if (!(digest == entry.manifest.artifacts[i].digest)) {
                error = "artifact digest mismatch: " + entry.manifest.artifacts[i].name;
                return false;
            }
            offset += size;
        }
        return true;
    }
    for (size_t i = 0; i < entry.manifest.artifacts.size(); ++i) {
        std::string content;
        if (!read_entry_payload(file, entry, i, content, error)) return false;
        if (!(digest_bytes(content.data(), content.size(), digest_domain_artifact)
                == entry.manifest.artifacts[i].digest)) {
            error = "artifact digest mismatch: " + entry.manifest.artifacts[i].name;
            return false;
        }
    }
    return true;
}

}   // namespace

std::string normalize_instantiation_argument(const std::string &spelling) { return normalise_spelling(spelling); }

Hash128 instantiation_request_id(const InstantiationRequest &request) {
    std::string key = "ABIX-INSTANTIATION-REQUEST\x1f" + request.template_name;
    for (const auto &argument : request.arguments)
        key += "\x1f" + normalize_instantiation_argument(argument);
    key += "\x1f@";
    key += std::to_string(request.arch);
    key += ":";
    key += std::to_string(request.os);
    key += ":";
    key += std::to_string(request.target_abi);
    key += ":";
    key += std::to_string(request.pointer_bits);
    key += "\x1f" + request.contract;
    return hash_text(key, kRequestDomain);
}

Hash128 instantiation_template_signature(const InstantiationRequest &request) {
    std::string key = "ABIX-INSTANTIATION-TEMPLATE\x1f" + request.template_name;
    for (const auto &argument : request.arguments)
        key += "\x1f" + normalize_instantiation_argument(argument);
    key += "\x1f" + request.contract;
    return hash_text(key, kTemplateDomain);
}

Hash128 instantiation_build_key(const InstantiationBuildInputs &inputs) {
    const Hash128 request_id = instantiation_request_id(inputs.request);
    std::string key = "ABIX-INSTANTIATION-BUILDKEY\x1f";
    key += std::to_string(request_id.lo) + ":" + std::to_string(request_id.hi) + "\x1f";
    key += std::to_string(inputs.source_digest.lo) + ":" + std::to_string(inputs.source_digest.hi) + "\x1f";
    key += std::to_string(inputs.toolchain.lo) + ":" + std::to_string(inputs.toolchain.hi) + "\x1f";
    key += std::to_string(inputs.build_options.lo) + ":" + std::to_string(inputs.build_options.hi) + "\x1f";
    key += std::to_string(inputs.protocol_version) + ":" + std::to_string(inputs.schema_version);
    return hash_text(key, digest_domain_build_key);
}

Hash128 digest_bytes(const void *data, size_t size, uint64_t domain) {
    return hash_text(std::string_view(static_cast<const char *>(data), size), domain);
}

Hash128 digest_text(const std::string &text, uint64_t domain) { return hash_text(text, domain); }

const char *cache_status_name(CacheStatus status) { return reflect::enum_name(status, "unknown"); }

InstantiationCache::InstantiationCache(std::string root)
    : _root(std::move(root)) {}

std::string InstantiationCache::entry_file(const Hash128 &build_key) const {
    return (fs::path(_root) / (hash_hex_plain(build_key) + kEntrySuffix)).string();
}

bool InstantiationCache::store(const InstantiationBuildInputs &inputs,
    const std::vector<std::pair<std::string, std::string>> &artifacts, std::string &error) {
    return store(inputs, artifacts, {}, error);
}

bool InstantiationCache::store(const InstantiationBuildInputs &inputs,
    const std::vector<std::pair<std::string, std::string>> &artifacts, const std::vector<CacheDependency> &dependencies,
    std::string &error) {
    error.clear();
    for (const auto &artifact : artifacts)
        if (!valid_artifact_name(artifact.first)) {
            error = "unsafe artifact name: " + artifact.first;
            return false;
        }
    Manifest manifest;
    manifest.schema_version = inputs.schema_version;
    manifest.protocol_version = inputs.protocol_version;
    manifest.build_key = instantiation_build_key(inputs);
    manifest.request = inputs.request;
    manifest.template_signature = instantiation_template_signature(inputs.request);
    manifest.request_id = instantiation_request_id(inputs.request);
    manifest.source_digest = inputs.source_digest;
    manifest.toolchain = inputs.toolchain;
    manifest.build_options = inputs.build_options;
    manifest.dependencies = dependencies;

    uint64_t payload_size = 0;
    for (const auto &artifact : artifacts) {
        CacheArtifact record;
        record.name = artifact.first;
        record.digest = digest_bytes(artifact.second.data(), artifact.second.size(), digest_domain_artifact);
        manifest.artifacts.push_back(std::move(record));
        if (add_overflows(payload_size, artifact.second.size(), payload_size)) {
            error = "cache payload is too large";
            return false;
        }
    }
    const std::string manifest_body = manifest_text(manifest);
    if (manifest_body.size() > UINT32_MAX) {
        error = "cache manifest is too large";
        return false;
    }

    std::error_code ec;
    fs::create_directories(_root, ec);
    if (ec) {
        error = "cannot create cache root: " + ec.message();
        return false;
    }
    const fs::path entry = entry_file(manifest.build_key);

    // Only a no-op when the committed entry is intact AND records the same
    // dependency closure. The key is project-level (it does not include file
    // contents), so a source change produces the same key but a different
    // closure, which must replace the stale entry rather than be ignored.
    const auto equivalent = [&](const fs::path &path) {
        EntryFile existing;
        std::string ignored;
        return read_entry_file(path, existing, ignored) == EntryRead::OK
            && existing.manifest.build_key == manifest.build_key
            && same_dependencies(existing.manifest.dependencies, manifest.dependencies)
            && verify_entry_payload(path, existing, ignored);
    };
    if (equivalent(entry)) return true;

    std::string blob(kEntryMagic, kEntryMagic + 8);
    put_u32(blob, kEntryFormatVersion);
    put_u32(blob, 0);
    put_u32(blob, static_cast<uint32_t>(manifest_body.size()));
    put_u32(blob, static_cast<uint32_t>(artifacts.size()));
    for (const auto &artifact : artifacts)
        put_u64(blob, artifact.second.size());
    blob += manifest_body;

    // Write the whole entry to a unique temporary file, then commit by rename.
    const std::string suffix = hash_hex_plain(manifest.build_key)
                             + "-"
                             + std::to_string(amc::support::process_id())
                             + "-"
                             + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                             + "-"
                             + std::to_string(next_sequence());
    const fs::path temporary = fs::path(_root) / (".tmp-" + suffix);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot write cache entry: " + temporary.string();
            return false;
        }
        output.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        for (const auto &artifact : artifacts)
            output.write(artifact.second.data(), static_cast<std::streamsize>(artifact.second.size()));
        output.close();
        if (!output) {
            fs::remove(temporary, ec);
            error = "cannot write cache entry: " + temporary.string();
            return false;
        }
    }
    // The bytes were hashed from memory and the write reported success, so only
    // the on-disk size is checked before publishing. verify()/lookup() still
    // re-hash whatever they read back.
    const uint64_t expected_size = blob.size() + payload_size;
    const auto written = fs::file_size(temporary, ec);
    if (ec || static_cast<uint64_t>(written) != expected_size) {
        fs::remove(temporary, ec);
        error = "cannot write cache entry: " + temporary.string();
        return false;
    }

    // Another writer may have committed a valid entry in the meantime; a missing
    // or damaged one is replaced below. `equivalent` already opens the entry, so
    // no separate existence check is needed.
    if (equivalent(entry)) {
        fs::remove(temporary, ec);
        return true;
    }
    fs::remove(entry, ec);
    fs::rename(temporary, entry, ec);
    if (ec) {
        // Another writer may have won between the check and the rename.
        if (equivalent(entry)) {
            fs::remove(temporary, ec);
            return true;
        }
        fs::remove(entry, ec);
        fs::rename(temporary, entry, ec);
        if (ec) {
            fs::remove(temporary, ec);
            error = "cannot commit cache entry: " + ec.message();
            return false;
        }
    }
    return true;
}

CacheLookup InstantiationCache::lookup(const InstantiationBuildInputs &inputs, const LookupOptions options) const {
    CacheLookup result;
    result.build_key = instantiation_build_key(inputs);
    result.entry = entry_file(result.build_key);
    const fs::path entry = result.entry;
    std::error_code ec;
#if !defined(_WIN32)
    // POSIX: a missing entry is cheaper to detect with stat() than by
    // constructing an ifstream. Windows keeps the direct-open path because
    // CreateFile-based metadata operations are much more expensive there.
    if (!fs::exists(entry, ec)) {
        result.status = CacheStatus::MISS;
        result.reason = cache_reason_not_found;
    } else
#endif
    {
        EntryFile file;
        std::string error;
        const EntryRead read = read_entry_file(entry, file, error);
        if (read == EntryRead::MISSING) {
            result.status = CacheStatus::MISS;
            result.reason = cache_reason_not_found;
        } else if (read == EntryRead::MALFORMED) {
            result.status = CacheStatus::INVALID_MANIFEST;
            result.reason = cache_reason_invalid_manifest;
        } else if (file.manifest.schema_version != instantiation_schema_version
                   || file.manifest.protocol_version != instantiation_protocol_version) {
            result.status = CacheStatus::SCHEMA_MISMATCH;
            result.reason = file.manifest.schema_version != instantiation_schema_version
                              ? cache_reason_schema_version_changed
                              : cache_reason_protocol_version_changed;
        } else if (options.verify_artifacts && !verify_entry_payload(entry, file, error)) {
            result.status = CacheStatus::CORRUPTED;
            result.reason = cache_reason_corrupted_artifact;
        } else {
            // The entry is only reusable while every recorded dependency still
            // hashes the same. This is what makes header changes invalidate
            // without the cache key having to list them.
            bool dependencies_ok = true;
            for (const auto &dependency : file.manifest.dependencies)
                if (!dependency_intact(dependency)) {
                    dependencies_ok = false;
                    break;
                }
            if (!dependencies_ok) {
                result.status = CacheStatus::MISS;
                result.reason = cache_reason_dependency_changed;
                return result;
            }
            result.status = CacheStatus::HIT;
            result.reason = cache_reason_hit;
            result.artifacts = file.manifest.artifacts;
            result.dependencies = file.manifest.dependencies;
            return result;
        }
    }
    // Fast path: only the "entry not found" case needs the diagnostic scan.
    // Invalid manifests, corrupted entries, schema drift and dependency changes
    // already carry a precise status/reason.
    if (!options.diagnose || result.status != CacheStatus::MISS || result.reason != cache_reason_not_found)
        return result;

    // Diagnose a miss against any same-template entry so incremental builds can
    // report why they rebuilt.
    const Hash128 desired_template = instantiation_template_signature(inputs.request);
    const Hash128 desired_request = instantiation_request_id(inputs.request);
    for (const auto &item : fs::directory_iterator(fs::path(_root), ec)) {
        if (!item.is_regular_file(ec)) continue;
        if (item.path().extension() != kEntrySuffix) continue;
        EntryFile other;
        std::string ignored;
        if (read_entry_file(item.path(), other, ignored) != EntryRead::OK) continue;
        if (!(other.manifest.template_signature == desired_template)) continue;
        if (!(other.manifest.request_id == desired_request)) {
            result.reason = cache_reason_target_changed;
            break;
        }
        if (other.manifest.protocol_version != inputs.protocol_version) {
            result.reason = cache_reason_protocol_version_changed;
        } else if (other.manifest.schema_version != inputs.schema_version) {
            result.reason = cache_reason_schema_version_changed;
        } else if (!(other.manifest.source_digest == inputs.source_digest)) {
            result.reason = cache_reason_source_digest_changed;
        } else if (!(other.manifest.toolchain == inputs.toolchain)) {
            result.reason = cache_reason_toolchain_changed;
        } else if (!(other.manifest.build_options == inputs.build_options)) {
            result.reason = cache_reason_build_options_changed;
        } else {
            continue;
        }
        break;
    }
    return result;
}

bool InstantiationCache::read_artifact(
    const CacheLookup &lookup, const std::string &name, std::string &out, std::string &error) const {
    error.clear();
    EntryFile file;
    if (read_entry_file(fs::path(lookup.entry), file, error) != EntryRead::OK) return false;
    for (size_t i = 0; i < file.manifest.artifacts.size(); ++i) {
        if (file.manifest.artifacts[i].name != name) continue;
        if (!read_entry_payload(fs::path(lookup.entry), file, i, out, error)) return false;
        if (!(digest_bytes(out.data(), out.size(), digest_domain_artifact) == file.manifest.artifacts[i].digest)) {
            error = "artifact digest mismatch: " + name;
            return false;
        }
        return true;
    }
    error = "artifact is not cached: " + name;
    return false;
}

bool InstantiationCache::verify(const Hash128 &build_key, std::string &error) const {
    const fs::path entry = entry_file(build_key);
    EntryFile file;
    if (read_entry_file(entry, file, error) != EntryRead::OK) return false;
    if (file.manifest.schema_version != instantiation_schema_version) {
        error = "artifact schema version mismatch";
        return false;
    }
    if (file.manifest.protocol_version != instantiation_protocol_version) {
        error = "instantiation protocol version mismatch";
        return false;
    }
    return verify_entry_payload(entry, file, error);
}

size_t InstantiationCache::verify_all(std::vector<std::string> &failures) const {
    std::error_code ec;
    size_t checked = 0;
    for (const auto &item : fs::directory_iterator(fs::path(_root), ec)) {
        if (!item.is_regular_file(ec)) continue;
        if (item.path().extension() != kEntrySuffix) continue;
        ++checked;
        EntryFile file;
        std::string error;
        if (read_entry_file(item.path(), file, error) != EntryRead::OK
            || !verify_entry_payload(item.path(), file, error))
            failures.push_back(item.path().filename().string() + ": " + error);
    }
    return checked;
}

size_t InstantiationCache::entries() const {
    std::error_code ec;
    size_t count = 0;
    for (const auto &item : fs::directory_iterator(fs::path(_root), ec)) {
        if (!item.is_regular_file(ec)) continue;
        if (item.path().extension() == kEntrySuffix) ++count;
    }
    return count;
}

size_t InstantiationCache::clear() const {
    std::error_code ec;
    std::vector<fs::path> remove;
    // Only the top level is walked: shard directories are removed whole, so a
    // recursive iterator would be invalidated while it is being deleted.
    for (const auto &item : fs::directory_iterator(fs::path(_root), ec)) {
        if (item.is_directory(ec)) {
            // Entries used to be directories; remove leftovers too.
            remove.push_back(item.path());
            continue;
        }
        const std::string name = item.path().filename().string();
        if (item.path().extension() == kEntrySuffix || name.rfind(".tmp-", 0) == 0) remove.push_back(item.path());
    }
    for (const auto &path : remove)
        fs::remove_all(path, ec);
    return remove.size();
}

void InstantiationCache::record_lookup(CacheStatus status) const {
    CacheCounters counters = read_counters(fs::path(_root));
    if (status == CacheStatus::HIT)
        ++counters.hits;
    else
        ++counters.misses;
    write_counters(fs::path(_root), counters);
}

void InstantiationCache::record_store() const {
    CacheCounters counters = read_counters(fs::path(_root));
    ++counters.stores;
    write_counters(fs::path(_root), counters);
}

CacheStats InstantiationCache::stats() const {
    CacheStats result;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    bool first = true;
    for (const auto &item : fs::directory_iterator(fs::path(_root), ec)) {
        if (!item.is_regular_file(ec)) continue;
        if (item.path().extension() != kEntrySuffix) continue;
        ++result.entries;
        const auto bytes = fs::file_size(item.path(), ec);
        if (!ec) result.bytes += bytes;
        const auto mtime = fs::last_write_time(item.path(), ec);
        if (ec) continue;
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - mtime).count();
        const uint64_t seconds = age < 0 ? 0 : static_cast<uint64_t>(age);
        if (first) {
            result.oldest_age_seconds = seconds;
            result.newest_age_seconds = seconds;
            first = false;
        } else {
            if (seconds > result.oldest_age_seconds) result.oldest_age_seconds = seconds;
            if (seconds < result.newest_age_seconds) result.newest_age_seconds = seconds;
        }
    }
    const CacheCounters counters = read_counters(fs::path(_root));
    result.hits = counters.hits;
    result.misses = counters.misses;
    result.stores = counters.stores;
    return result;
}

PruneResult InstantiationCache::prune(const PruneOptions &options, bool dry_run) const {
    struct Item {
        fs::path path;
        uint64_t bytes = 0;
        fs::file_time_type mtime{};
    };
    PruneResult result;
    std::error_code ec;
    std::vector<Item> items;
    uint64_t total = 0;
    for (const auto &entry : fs::directory_iterator(fs::path(_root), ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != kEntrySuffix) continue;
        const auto bytes = fs::file_size(entry.path(), ec);
        if (ec) continue;
        const auto mtime = fs::last_write_time(entry.path(), ec);
        if (ec) continue;
        items.push_back({entry.path(), static_cast<uint64_t>(bytes), mtime});
        total += static_cast<uint64_t>(bytes);
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.mtime < b.mtime; });

    const auto now = fs::file_time_type::clock::now();
    for (const auto &item : items) {
        bool remove = false;
        if (options.max_age_seconds != 0) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - item.mtime).count();
            if (age >= 0 && static_cast<uint64_t>(age) > options.max_age_seconds) remove = true;
        }
        if (!remove && options.max_bytes != 0 && total > options.max_bytes) remove = true;
        if (!remove) continue;
        if (!dry_run) fs::remove(item.path, ec);
        total -= item.bytes;
        ++result.removed;
        result.freed_bytes += item.bytes;
    }
    return result;
}

}   // namespace skl::amc
