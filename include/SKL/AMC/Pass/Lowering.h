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
#ifndef AMC_PASS_LOWERING_H
#define AMC_PASS_LOWERING_H

#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Lowering.h"
#include "SKL/AMC/IR/Module.h"

namespace skl::amc::pass {

struct LoweringOptions {
    bool strict = false;   // report rejected entities as errors instead of warnings
};

// M4: link semantic entities to their ABI counterparts. The pass does not
// rewrite the ABI module: it produces an explicit LoweringResult that backends
// and tools can consume without re-interpreting source semantics.
bool lower(const ir::Module &module, ir::LoweringResult &result, diagnostics::DiagnosticList &diagnostics,
    const LoweringOptions &options = {});

// Pipeline entry: write the lowering result back into the IR module. Lowering
// errors stay warnings by default so an artifact can still carry partial
// contracts; strict callers use `lower(..., LoweringOptions{true})`.
bool lower_pass(ir::Module &module, diagnostics::DiagnosticList &diagnostics);

}   // namespace skl::amc::pass

#endif   // AMC_PASS_LOWERING_H
