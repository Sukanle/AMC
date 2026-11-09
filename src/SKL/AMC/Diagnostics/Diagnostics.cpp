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
#include "SKL/AMC/Diagnostics/Diagnostics.h"

#include "SKL/AMC/Diagnostics/Error.h"

#include <fmt/format.h>

namespace skl::amc::diagnostics {

const char *severity_name(Severity severity) {
    switch (severity) {
        case Severity::NOTE:    return "note";
        case Severity::WARNING: return "warning";
        case Severity::ERROR:   return "error";
    }
    return "error";
}

Summary summarize(const DiagnosticList &diagnostics) {
    Summary summary;
    for (const auto &diagnostic : diagnostics) {
        switch (diagnostic.severity) {
            case Severity::NOTE:    ++summary.notes; break;
            case Severity::WARNING: ++summary.warnings; break;
            case Severity::ERROR:   ++summary.errors; break;
        }
    }
    return summary;
}

std::string diagnostics_to_text(const DiagnosticList &diagnostics) {
    std::string output;
    for (const auto &diagnostic : diagnostics) {
        if (!diagnostic.file.empty()) {
            output += diagnostic.file;
            if (diagnostic.line != 0) {
                output += ":";
                output += fmt::to_string(diagnostic.line);
                if (diagnostic.column != 0) {
                    output += ":";
                    output += fmt::to_string(diagnostic.column);
                }
            }
            output += ": ";
        }
        output += severity_name(diagnostic.severity);
        output += ": ";
        if (diagnostic.file.empty()) {
            if (!diagnostic.stage.empty()) {
                output += diagnostic.stage;
                output += ": ";
            }
            if (!diagnostic.code.empty()) {
                output += diagnostic.code;
                output += ": ";
            }
        }
        output += diagnostic.message;
        output += '\n';
    }
    return output;
}

std::string diagnostics_to_json(const DiagnosticList &diagnostics) {
    constexpr const char *kSchema = "amc.diagnostics/1";
    const Summary summary = summarize(diagnostics);
    std::string output = fmt::format(
        "{{\"schema\":\"{}\",\"status\":\"{}\",\"notes\":{},\"warnings\":{},\"errors\":{},\"diagnostics\":[", kSchema,
        summary.ok() ? "ok" : "error", summary.notes, summary.warnings, summary.errors);
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const auto &diagnostic = diagnostics[i];
        if (i != 0) output += ',';
        output += "{\"severity\":\"";
        output += severity_name(diagnostic.severity);
        output += "\",\"code\":\"";
        output += amc::json_escape(diagnostic.code);
        output += "\",\"stage\":\"";
        output += amc::json_escape(diagnostic.stage);
        output += "\",\"message\":\"";
        output += amc::json_escape(diagnostic.message);
        output += '"';
        if (!diagnostic.file.empty()) {
            output += ",\"file\":\"";
            output += amc::json_escape(diagnostic.file);
            output += '"';
        }
        if (diagnostic.line != 0) output += fmt::format(",\"line\":{}", diagnostic.line);
        if (diagnostic.column != 0) output += fmt::format(",\"column\":{}", diagnostic.column);
        output += '}';
    }
    output += "]}";
    return output;
}

}   // namespace skl::amc::diagnostics
