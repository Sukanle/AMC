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
#ifndef AMC_PASS_TYPE_MAPPING_H
#define AMC_PASS_TYPE_MAPPING_H

#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/Pass/Pass.h"

namespace skl::amc::pass {

// A3: type-mapping compatibility report. The matching policy remains in
// Core/Adapter; the pass owns the error/diagnostic boundary used by CLI and
// Compilation.
bool build_compatibility_report(const ABIModule &source, const ABIModule &target, ABIModule &report, Context &context,
    diagnostics::DiagnosticList &diagnostics);

}   // namespace skl::amc::pass

#endif   // AMC_PASS_TYPE_MAPPING_H
