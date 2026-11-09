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
#include "SKL/AMC/Backend/Cpp/Cpp.h"

#include "SKL/AMC/Private/Backend/Cpp/Emitters.h"
#include "SKL/AMC/Core/Core.h"
#include "SKL/AMC/IR/Artifact.h"
#include "SKL/AMC/IR/LoweringJson.h"
#include "SKL/AMC/Lang/Lang.h"

#include <fmt/os.h>

#include <string>

namespace skl::amc::lang::cpp {

namespace backend = backend::cpp;

int run_backend(const char *input, const char *output) {
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;
    amc::ir::Module artifact;
    std::string e;
    // Backends project the ABI and lowering contract; the semantic sidecar is
    // not consumed on this path, so skip parsing it.
    amc::ir::LoadOptions load_options;
    load_options.semantic = false;
    if (!amc::ir::load_artifact(input, artifact, e, load_options)) {
        fmt::print(stderr, "{}\n", e);
        return 1;
    }
    amc::ABIModule m = std::move(artifact.abi);
    amc::ir::LoweringResult lowering = std::move(artifact.lowering);
    std::string lowering_error;
    if (!amc::ir::apply_lowering_contracts(lowering, m, lowering_error)) {
        fmt::print(stderr, "{}\n", lowering_error);
        return 1;
    }
    if (amc::lang::cancellation_requested()) return amc::lang::kCapabilityCancelled;

    auto o = fmt::output_file(output);
    std::string error;
    const auto report = [&](int status) {
        if (!error.empty()) fmt::print(stderr, "{}\n", error);
        return status;
    };
    backend::Emit emit(o, m);
    if (const int status = emit.preamble(error); status != 0) return report(status);
    if (const int status = emit.projection(error); status != 0) return report(status);
    emit.interfaces();
    emit.handles(lowering);
    emit.named_error_sets();
    emit.error_boundaries();
    emit.native_type_traits();
    emit.static_checks(output);
    return 0;
}

}   // namespace skl::amc::lang::cpp
