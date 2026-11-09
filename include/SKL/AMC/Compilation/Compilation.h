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
#ifndef AMC_COMPILATION_COMPILATION_H
#define AMC_COMPILATION_COMPILATION_H

#include "SKL/AMC/Backend/Backend.h"
#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Module.h"

#include <filesystem>
#include <vector>

namespace skl::amc::compilation {

// A0 contract: CLI, MCP, LSP and SDK call this API instead of implementing
// their own configure/frontend/pass/backend sequence.
struct Request {
    std::filesystem::path config_path;
    std::filesystem::path build_directory;
    bool use_cache = true;
};

struct Result {
    ir::Module module;
    std::vector<backend::Artifact> artifacts;
    diagnostics::DiagnosticList diagnostics;

    [[nodiscard]] bool ok() const { return !diagnostics::has_errors(diagnostics); }
};

class Compiler {
public:
    virtual ~Compiler() = default;
    virtual Result compile(const Request &request) = 0;
};

}   // namespace skl::amc::compilation

#endif   // AMC_COMPILATION_COMPILATION_H
