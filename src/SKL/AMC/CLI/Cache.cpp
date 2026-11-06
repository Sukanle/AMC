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
#include "SKL/AMC/CLI/Support.h"
#include "SKL/AMC/Core/Adapter.h"
#include "SKL/AMC/Compilation/Cache.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Error.h"
#include "SKL/AMC/Artifact/Metadata.h"
#include "SKL/AMC/Util/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace fs = std::filesystem;
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

namespace amc::cli {
namespace {

bool parse_size(const std::string &text, uint64_t &out) {
    if (text.empty()) return false;
    uint64_t multiplier = 1;
    std::string digits = text;
    const char suffix = digits.back();
    if (suffix == 'k' || suffix == 'K') {
        multiplier = 1024ull;
        digits.pop_back();
    } else if (suffix == 'm' || suffix == 'M') {
        multiplier = 1024ull * 1024;
        digits.pop_back();
    } else if (suffix == 'g' || suffix == 'G') {
        multiplier = 1024ull * 1024 * 1024;
        digits.pop_back();
    }
    if (digits.empty()) return false;
    uint64_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    out = value * multiplier;
    return true;
}

bool parse_days(const std::string &text, uint64_t &out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    out = value;
    return true;
}

std::string human_bytes(uint64_t bytes) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }
    return fmt::format("{:.1f} {}", value, units[unit]);
}

std::string human_age(uint64_t seconds) {
    if (seconds < 60) return fmt::format("{}s", seconds);
    if (seconds < 3600) return fmt::format("{}m", seconds / 60);
    if (seconds < 86400) return fmt::format("{}h", seconds / 3600);
    return fmt::format("{}d", seconds / 86400);
}

}   // namespace

// `amc cache <dir> [stats|prune] [--verify|--clear] [--format text|json]`
//
// Inspects the M3.4 instantiation request cache: entry count, integrity
// verification, clearing, usage statistics and LRU pruning. Building is what
// populates the cache; this command is the diagnostic and CI surface over it.
int cmd_cache(char **args, int arg_count) {
    fs::path root;
    std::string command;
    bool verify = false;
    bool clear = false;
    bool json = false;
    bool dry_run = false;
    uint64_t max_bytes = 0;
    uint64_t max_age_days = 0;
    for (int i = 2; i < arg_count; ++i) {
        const std::string argument = args[i];
        if (argument == "--verify") {
            verify = true;
        } else if (argument == "--clear") {
            clear = true;
        } else if (argument == "--dry-run") {
            dry_run = true;
        } else if (argument == "stats" || argument == "prune") {
            if (!command.empty()) return usage_error("cache accepts a single command");
            command = argument;
        } else if (argument == "--max-size" && i + 1 < arg_count) {
            if (!parse_size(args[++i], max_bytes)) return usage_error("--max-size expects a size like 512M");
        } else if (argument.rfind("--max-size=", 0) == 0) {
            if (!parse_size(argument.substr(std::string("--max-size=").size()), max_bytes))
                return usage_error("--max-size expects a size like 512M");
        } else if (argument == "--max-age" && i + 1 < arg_count) {
            if (!parse_days(args[++i], max_age_days)) return usage_error("--max-age expects a number of days");
        } else if (argument.rfind("--max-age=", 0) == 0) {
            if (!parse_days(argument.substr(std::string("--max-age=").size()), max_age_days))
                return usage_error("--max-age expects a number of days");
        } else if (argument == "--format" && i + 1 < arg_count) {
            const std::string value = args[++i];
            if (value != "text" && value != "json") return usage_error("--format must be text or json");
            json = value == "json";
        } else if (argument.rfind("--format=", 0) == 0) {
            const std::string value = argument.substr(std::string("--format=").size());
            if (value != "text" && value != "json") return usage_error("--format must be text or json");
            json = value == "json";
        } else if (root.empty()) {
            root = argument;
        } else {
            return usage_error("cache accepts a single directory");
        }
    }
    if (root.empty()) return usage_error("cache requires a directory");
    if (!command.empty() && (clear || verify))
        return usage_error("stats/prune cannot be combined with --clear or --verify");

    amc::InstantiationCache cache(root.string());

    if (clear) {
        const size_t removed = cache.clear();
        if (json)
            fmt::print("{{\"schema\":\"abix.cache/1\",\"cleared\":{}}}\n", removed);
        else
            fmt::print("cache cleared: {} entries\n", removed);
        return 0;
    }

    if (verify) {
        std::vector<std::string> failures;
        const size_t checked = cache.verify_all(failures);
        if (json) {
            fmt::print("{{\"schema\":\"abix.cache/1\",\"checked\":{},\"failures\":[", checked);
            for (size_t i = 0; i < failures.size(); ++i)
                fmt::print("{}{}", i == 0 ? "" : ",", json_quote(failures[i]));
            fmt::print("]}}\n");
        } else {
            for (const auto &failure : failures)
                fmt::print("corrupt: {}\n", failure);
            fmt::print("cache verified: {} entries, {} failure(s)\n", checked, failures.size());
        }
        return failures.empty() ? 0 : 1;
    }

    if (command == "stats") {
        const amc::CacheStats stats = cache.stats();
        const uint64_t lookups = stats.hits + stats.misses;
        const double hit_rate = lookups == 0 ? 0.0 : static_cast<double>(stats.hits) / static_cast<double>(lookups);
        if (json) {
            fmt::print(
                "{{\"schema\":\"abix.cache/1\",\"root\":{},\"entries\":{},\"bytes\":{},\"hits\":{},\"misses\":{},"
                "\"stores\":{},\"hit_rate\":{:.4f},\"oldest_age_seconds\":{},\"newest_age_seconds\":{}}}\n",
                json_quote(root.string()), stats.entries, stats.bytes, stats.hits, stats.misses, stats.stores,
                hit_rate, stats.oldest_age_seconds, stats.newest_age_seconds);
        } else {
            fmt::print("cache {}\n", root.string());
            fmt::print("  entries: {}\n", stats.entries);
            fmt::print("  size:    {}\n", human_bytes(stats.bytes));
            if (lookups == 0) {
                fmt::print("  lookups: none recorded\n");
            } else {
                fmt::print("  lookups: {} hit, {} miss ({:.1f}% hit rate)\n", stats.hits, stats.misses,
                    hit_rate * 100.0);
            }
            if (stats.entries != 0)
                fmt::print("  age:     newest {}, oldest {}\n", human_age(stats.newest_age_seconds),
                    human_age(stats.oldest_age_seconds));
        }
        return 0;
    }

    if (command == "prune") {
        amc::PruneOptions options;
        options.max_bytes = max_bytes;
        options.max_age_seconds = max_age_days * 86400;
        if (options.max_bytes == 0 && options.max_age_seconds == 0)
            return usage_error("prune needs --max-size and/or --max-age");
        const amc::PruneResult result = cache.prune(options, dry_run);
        if (json)
            fmt::print("{{\"schema\":\"abix.cache/1\",\"dry_run\":{},\"removed\":{},\"freed_bytes\":{}}}\n", dry_run,
                result.removed, result.freed_bytes);
        else
            fmt::print("{} {} entries, {}\n", dry_run ? "would remove" : "removed", result.removed,
                human_bytes(result.freed_bytes));
        return 0;
    }

    const size_t entries = cache.entries();
    if (json)
        fmt::print("{{\"schema\":\"abix.cache/1\",\"root\":{},\"entries\":{}}}\n", json_quote(root.string()), entries);
    else
        fmt::print("cache {}: {} entries\n", root.string(), entries);
    return 0;
}

}   // namespace amc::cli
