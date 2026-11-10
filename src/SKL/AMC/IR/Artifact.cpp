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
#include "SKL/AMC/IR/Artifact.h"

#include "SKL/AMC/Private/Support/File.h"

#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Semantic/Serialize.h"


namespace skl::amc::ir {
namespace {

bool exists(const std::string &path) { return amc::support::path_exists(path); }

}   // namespace

ArtifactPaths artifact_paths(const std::string &abi_path) {
    return {abi_path, abi_path + ".semantic", abi_path + ".lowering"};
}

bool load_artifact(const std::string &abi_path, Module &module, std::string &error, LoadOptions options) {
    module = {};
    if (!amc::read_abix(abi_path, module.abi, error)) return false;

    const ArtifactPaths paths = artifact_paths(abi_path);
    if (options.semantic && exists(paths.semantic)) {
        if (!semantic::read_semantic_json(paths.semantic, module.semantic, error)) return false;
        module.artifact.semantic_loaded = true;
    }
    if (options.lowering && exists(paths.lowering)) {
        if (!read_lowering_json(paths.lowering, module.lowering, error)) return false;
        module.artifact.lowering_loaded = true;
    }
    return true;
}

}   // namespace skl::amc::ir
