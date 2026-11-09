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
#ifndef AMC_DIAGNOSTICS_DIAGNOSTICS_H
#define AMC_DIAGNOSTICS_DIAGNOSTICS_H

#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace skl::amc::diagnostics {

enum class Severity : uint32_t {
    NOTE,
    WARNING,
    ERROR,
};

struct Diagnostic {
    Severity severity = Severity::ERROR;
    std::string code;    // stable machine-readable code
    std::string stage;   // frontend / pass / backend / compilation
    std::string message;
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
};

using DiagnosticList = std::vector<Diagnostic>;

inline bool has_errors(const DiagnosticList &diagnostics) {
    return std::any_of(
        diagnostics.begin(), diagnostics.end(), [](const Diagnostic &e) { return e.severity == Severity::ERROR; });
}

inline size_t error_count(const DiagnosticList &diagnostics) {
    return static_cast<size_t>(std::count_if(
        diagnostics.begin(), diagnostics.end(), [](const Diagnostic &d) { return d.severity == Severity::ERROR; }));
}

inline void add_error(DiagnosticList &diagnostics, std::string code, std::string stage, std::string message) {
    diagnostics.push_back({Severity::ERROR, std::move(code), std::move(stage), std::move(message), {}, 0, 0});
}

inline void add_warning(DiagnosticList &diagnostics, std::string code, std::string stage, std::string message) {
    diagnostics.push_back({Severity::WARNING, std::move(code), std::move(stage), std::move(message), {}, 0, 0});
}

struct Summary {
    size_t notes = 0;
    size_t warnings = 0;
    size_t errors = 0;

    [[nodiscard]] bool ok() const noexcept { return errors == 0; }
};

const char *severity_name(Severity severity);
Summary summarize(const DiagnosticList &diagnostics);
std::string diagnostics_to_text(const DiagnosticList &diagnostics);
std::string diagnostics_to_json(const DiagnosticList &diagnostics);

}   // namespace skl::amc::diagnostics

#endif   // AMC_DIAGNOSTICS_DIAGNOSTICS_H
