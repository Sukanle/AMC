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
#pragma once

#include "SKL/AMC/Lang/Lang.h"
#include "SKL/AMC/Private/Frontend/Cpp/Native.h"
#include "SKL/AMC/Semantic/Model.h"

#include <llvm/ADT/StringRef.h>

#include <string>
#include <vector>

namespace amc {
struct AbiModule;
}

namespace amc::lang::frontend::cpp {

// Non-owning operation object that turns the `[[expose]]` declarations into a
// generated C thunk source file and registers the matching ABI entities.
// It holds only references; the frontend owns the module, config and semantic
// view for the duration of the run.
class ThunkEmitter {
public:
    ThunkEmitter(amc::AbiModule &module, const amc::lang::Config &config, std::vector<NativeAdapt> &adaptations,
        llvm::StringRef base, amc::semantic::SemanticModule &semantic);

    bool run(std::string &error);

private:
    amc::AbiModule &module_;
    const amc::lang::Config &config_;
    std::vector<NativeAdapt> &adaptations_;
    llvm::StringRef base_;
    amc::semantic::SemanticModule &semantic_;
};

}   // namespace amc::lang::frontend::cpp
