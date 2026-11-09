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
#ifndef AMC_PASS_VERIFY_H
#define AMC_PASS_VERIFY_H

#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Module.h"
#include "SKL/AMC/Pass/Pass.h"

namespace skl::amc::pass {

// A3: ABI validation is a pass, not a frontend/backend responsibility.
bool verify_abi(ir::Module &module, Context &context, diagnostics::DiagnosticList &diagnostics);

// A3: lowering references must resolve against the ABI view. This is the same
// consistency rule the backends apply, moved ahead of projection/emission.
bool verify_lowering(ir::Module &module, Context &context, diagnostics::DiagnosticList &diagnostics);

}   // namespace skl::amc::pass

#endif   // AMC_PASS_VERIFY_H
