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
#include "SKL/AMC/Pass/Pipeline.h"

#include "SKL/AMC/Pass/Lowering.h"
#include "SKL/AMC/Pass/Semantic.h"
#include "SKL/AMC/Pass/Verify.h"

namespace amc::pass {

Pipeline make_default_pipeline() { return make_pipeline({}); }

Pipeline make_pipeline(const PipelineOptions &options) {
    Pipeline pipeline;
    if (options.verify_semantic) pipeline.passes.push_back({"semantic-verify", verify_semantic});
    if (options.verify_abi) pipeline.passes.push_back({"abi-verify", verify_abi});
    if (options.lowering)
        pipeline.passes.push_back({"lowering",
            [](ir::Module &module, Context &, diagnostics::DiagnosticList &out) { return lower_pass(module, out); }});
    if (options.verify_lowering) pipeline.passes.push_back({"lowering-verify", verify_lowering});
    return pipeline;
}

bool run_default_pipeline(
    ir::Module &module, diagnostics::DiagnosticList &diagnostics, const PipelineOptions &options) {
    const Pipeline pipeline = make_pipeline(options);
    Context context;
    context.strict = options.strict;
    return pipeline.run(module, context, diagnostics);
}

}   // namespace amc::pass
