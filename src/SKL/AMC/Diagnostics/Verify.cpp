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
#include "SKL/AMC/Diagnostics/Verify.h"

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Core/Verify.h"

#include <fmt/format.h>

namespace amc::diagnostics {

namespace {

const amc::Type *find_type(const AbiModule &contract, const std::string &name) {
    for (const auto &type : contract.types)
        if (type.name == name) return &type;
    return nullptr;
}

void attach_source_origin(const AbiModule &contract, const std::string &type_name, Diagnostic &diagnostic) {
    if (type_name.empty()) return;
    const amc::Type *type = find_type(contract, type_name);
    if (type == nullptr) return;
    const amc::SourceOrigin *source = amc::find_source_origin(contract, type->id);
    if (source == nullptr) return;
    diagnostic.file = source->file;
    diagnostic.line = source->line;
    diagnostic.column = source->column;
}

std::string verify_message(const VerifyChange &change) {
    std::string message = fmt::format("ABI {}", amc::change_kind_name(change.kind));
    if (!change.type.empty()) {
        message += " for ";
        message += change.type;
    }
    if (!change.name.empty()) {
        message += ".";
        message += change.name;
    }
    if (!change.detail.empty()) {
        message += ": ";
        message += change.detail;
    }
    return message;
}

}   // namespace

DiagnosticList from_verify_result(const AbiModule &contract, const VerifyResult &result) {
    DiagnosticList diagnostics;
    diagnostics.reserve(result.changes.size());
    for (const auto &change : result.changes) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::error;
        diagnostic.code = "abi_" + std::string(amc::change_kind_name(change.kind));
        diagnostic.stage = "verify";
        diagnostic.message = verify_message(change);
        attach_source_origin(contract, change.type, diagnostic);
        diagnostics.push_back(std::move(diagnostic));
    }
    return diagnostics;
}

}   // namespace amc::diagnostics
