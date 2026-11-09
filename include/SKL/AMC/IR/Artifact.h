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
#ifndef AMC_IR_ARTIFACT_H
#define AMC_IR_ARTIFACT_H

#include "SKL/AMC/IR/Module.h"

#include <string>

namespace skl::amc::ir {

// A2 artifact boundary: the IR is the in-memory model; `.abix`, `.semantic`
// and `.lowering` are independent persistence contracts next to the artifact.
struct ArtifactPaths {
    std::string abi;
    std::string semantic;
    std::string lowering;
};

ArtifactPaths artifact_paths(const std::string &abi_path);

// Loading policy for the optional sidecars. Backends that only consume the
// ABI and lowering views can skip `.semantic` entirely, which avoids parsing
// the largest sidecar on the generation path.
struct LoadOptions {
    bool semantic = true;
    bool lowering = true;
};

// Loads the ABI artifact and its optional semantic/lowering sidecars into one
// IR module. A missing sidecar is not an error; a present but malformed sidecar
// is. `options` selects which sidecars are read. This keeps artifact naming and
// consistency in one place instead of duplicating `<path>.semantic` /
// `<path>.lowering` in every backend.
bool load_artifact(const std::string &abi_path, Module &module, std::string &error, LoadOptions options = {});

}   // namespace skl::amc::ir

#endif   // AMC_IR_ARTIFACT_H
