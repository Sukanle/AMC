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
#ifndef AMC_PASS_PASS_H
#define AMC_PASS_PASS_H

#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Module.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace amc::pass {

// A0 contract: passes own validation and IR transformation. Frontends and
// backends must not re-implement these checks. Passes mutate a working IR and
// report failures through the shared diagnostic list.
struct Context {
    uint32_t semantic_rules_version = 1;
    uint32_t abi_rules_version = 1;
    bool strict = true;
};

using PassFunction = std::function<bool(ir::Module &, Context &, diagnostics::DiagnosticList &)>;

struct Pass {
    std::string name;
    PassFunction run;
};

struct Pipeline {
    std::vector<Pass> passes;

    bool run(ir::Module &module, Context &context, diagnostics::DiagnosticList &diagnostics) const {
        for (const Pass &pass : passes) {
            if (!pass.run) continue;
            const size_t before = diagnostics::error_count(diagnostics);
            if (!pass.run(module, context, diagnostics)) {
                if (diagnostics::error_count(diagnostics) == before)
                    diagnostics::add_error(
                        diagnostics, "pass_failed", "pass", "pass failed without a diagnostic: " + pass.name);
                return false;
            }
            if (diagnostics::error_count(diagnostics) > before) return false;
        }
        return true;
    }
};

}   // namespace amc::pass

#endif   // AMC_PASS_PASS_H
