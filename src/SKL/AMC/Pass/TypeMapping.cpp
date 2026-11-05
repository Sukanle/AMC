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
#include "SKL/AMC/Pass/TypeMapping.h"

#include "SKL/AMC/IR/TypeMapping.h"

namespace amc::pass {

bool build_compatibility_report(const AbiModule &source, const AbiModule &target, AbiModule &report, Context &context,
    diagnostics::DiagnosticList &diagnostics) {
    (void)context;
    std::string error;
    if (!amc::ir::build_type_mapping(source, target, report, error)) {
        diagnostics::add_error(diagnostics, "compatibility_failed", "pass", error);
        return false;
    }
    return true;
}

}   // namespace amc::pass
