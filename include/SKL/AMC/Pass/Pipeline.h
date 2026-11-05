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
#ifndef AMC_PASS_PIPELINE_H
#define AMC_PASS_PIPELINE_H

#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Module.h"
#include "SKL/AMC/Pass/Pass.h"

namespace amc::pass {

struct PipelineOptions {
    bool verify_artifact = true;
    bool verify_semantic = true;
    bool verify_abi = true;
    bool lowering = true;
    bool verify_lowering = true;
    bool strict = true;
};

// A3 standard pipeline: validate the semantic and ABI views, run lowering, then
// verify the resulting lowering references.
Pipeline make_default_pipeline();
Pipeline make_pipeline(const PipelineOptions &options);

bool run_default_pipeline(
    ir::Module &module, diagnostics::DiagnosticList &diagnostics, const PipelineOptions &options = {});

}   // namespace amc::pass

#endif   // AMC_PASS_PIPELINE_H
