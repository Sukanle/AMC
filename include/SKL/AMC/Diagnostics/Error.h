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
#ifndef AMC_ERROR_H
#define AMC_ERROR_H

#include "SKL/AMC/Error.h"

#include <stdint.h>
#include <string>
#include <string_view>

namespace skl::amc {

// Unified AMC error schema.
//
// Every AMC tool (amc, amc-dump, ...) reports failures through this one
// object so that machine consumers (AI agents, MCP servers, CI) can parse a
// stable envelope instead of scraping human prose.  `ErrorCategory` is the
// coarse class, `stage` names the operation that failed, and `message`
// carries the human-readable detail.
enum class ErrorCategory : uint32_t {
    USAGE,
    CONFIG,
    IO,
    PROVIDER,
    FORMAT,
    VALIDATION,
    COMPATIBILITY,
    INTERNAL,
};

const char *error_category_name(ErrorCategory category);

// Maps the coarse diagnostic category onto the stable AMC domain. `category`
// stays available for human-readable output and old consumers.
[[nodiscard]] constexpr AMCErr amc_error_from_category(ErrorCategory category) noexcept {
    switch (category) {
        case ErrorCategory::USAGE:         return AMCErr::usage;
        case ErrorCategory::CONFIG:        return AMCErr::config;
        case ErrorCategory::IO:            return AMCErr::io;
        case ErrorCategory::PROVIDER:      return AMCErr::provider;
        case ErrorCategory::FORMAT:        return AMCErr::format;
        case ErrorCategory::VALIDATION:    return AMCErr::validation;
        case ErrorCategory::COMPATIBILITY: return AMCErr::compatibility;
        case ErrorCategory::INTERNAL:      return AMCErr::internal;
    }
    return AMCErr::internal;
}

// Stable, machine-readable error code derived from the category, e.g.
// "AMC-IO" / "AMC-FORMAT".  Kept deliberately coarse; `stage` narrows it.
std::string error_code(ErrorCategory category);

struct Error {
    // Stable EPCD identity. `category` below is the coarse compatibility and
    // presentation classification, not a second identity.
    skl::epcd::ErrorCode code{};
    ErrorCategory category = ErrorCategory::INTERNAL;
    std::string stage;
    std::string message;
    std::string file;     // optional offending artifact / path
    std::string detail;   // optional extra context (free-form)
};

Error make_error(
    ErrorCategory category, std::string stage, std::string message, std::string file = {}, std::string detail = {});

// Serialized error envelope, stable across releases:
//   {"schema":"abix.error/1","error":{"code":...,"category":...,
//    "stage":...,"message":...,"file":...,"detail":...}}
std::string error_to_json(const Error &error);

// Human-readable one-liner used when structured output is not requested:
//   amc: <category>/<stage>: <message> (file: <file>) [<detail>]
std::string error_to_text(const Error &error);

enum class ErrorFormat {
    TEXT,
    JSON,
};

bool parse_error_format(std::string_view value, ErrorFormat &format);
const char *error_format_name(ErrorFormat format);

// Escapes a UTF-8 string for embedding in a JSON string literal (no quotes).
std::string json_escape(std::string_view value);

}   // namespace skl::amc

#endif
