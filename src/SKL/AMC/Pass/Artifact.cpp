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
#include "SKL/AMC/Pass/Artifact.h"

#include <fmt/format.h>

namespace skl::amc::pass {

bool verify_artifact(ir::Module &module, Context &context, diagnostics::DiagnosticList &diagnostics) {
    (void)context;
    if (module.schema_version > ir::module_schema_version) {
        diagnostics::add_error(diagnostics, "artifact_schema_unsupported", "pass",
            fmt::format(
                "IR schema version {} is newer than supported {}", module.schema_version, ir::module_schema_version));
        return false;
    }
    if (module.artifact.semantic_loaded && module.semantic.rules_version == 0) {
        diagnostics::add_error(
            diagnostics, "artifact_semantic_invalid", "pass", "semantic sidecar has no rules version");
        return false;
    }
    return true;
}

}   // namespace skl::amc::pass
