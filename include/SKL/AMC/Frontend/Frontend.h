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
#ifndef AMC_FRONTEND_FRONTEND_H
#define AMC_FRONTEND_FRONTEND_H

#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Module.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace skl::amc::frontend {

// A0 contract: a frontend turns source-language input into IR. It must not
// depend on any backend, and it must report unsupported constructs through
// diagnostics instead of silently lowering them.
struct Request {
    std::string language;
    std::filesystem::path config_path;
    std::filesystem::path source_root;
    std::vector<std::filesystem::path> files;
    std::vector<std::string> arguments;
};

struct Result {
    ir::Module module;
    diagnostics::DiagnosticList diagnostics;
    std::vector<std::filesystem::path> dependencies;

    [[nodiscard]] bool ok() const { return !diagnostics::has_errors(diagnostics); }
};

struct Frontend {
    std::string language;
    std::function<Result(const Request &)> run;
};

}   // namespace skl::amc::frontend

#endif   // AMC_FRONTEND_FRONTEND_H
