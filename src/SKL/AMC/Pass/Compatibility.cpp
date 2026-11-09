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
#include "SKL/AMC/Pass/Compatibility.h"

#include "SKL/AMC/IR/Compatibility.h"

#include <fmt/format.h>

namespace skl::amc::pass {

bool compare_modules(const ABIModule &contract, const ABIModule &implementation, ir::CompatibilityResult &result,
    Context &context, diagnostics::DiagnosticList &diagnostics) {
    (void)context;
    std::string error;
    if (!amc::ir::compare_contracts(contract, implementation, result, error)) {
        diagnostics::add_error(diagnostics, "compatibility_failed", "pass", error);
        return false;
    }
    for (const auto &change : result.changes) {
        const std::string message = fmt::format("{}: {}{}", amc::change_kind_name(change.kind), change.type,
            change.name.empty() ? std::string{} : "::" + change.name);
        if (context.strict)
            diagnostics::add_error(diagnostics, "compatibility_drift", "pass", message);
        else
            diagnostics::add_warning(diagnostics, "compatibility_drift", "pass", message);
    }
    return true;
}

}   // namespace skl::amc::pass
