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
#ifndef AMC_BACKEND_BACKEND_H
#define AMC_BACKEND_BACKEND_H

#include "SKL/AMC/Diagnostics/Diagnostics.h"
#include "SKL/AMC/IR/Module.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace skl::amc::backend {

// A0 contract: a backend reads a validated IR and emits files. It must not
// parse source languages or include Clang AST headers.
struct Artifact {
    std::string name;
    std::filesystem::path path;
    std::vector<uint8_t> bytes;
};

struct Request {
    const ir::Module *module = nullptr;
    std::string language;
    std::filesystem::path output_directory;
};

struct Result {
    std::vector<Artifact> artifacts;
    diagnostics::DiagnosticList diagnostics;

    [[nodiscard]] bool ok() const { return !diagnostics::has_errors(diagnostics); }
};

struct Backend {
    std::string language;
    std::function<Result(const Request &)> generate;
};

}   // namespace skl::amc::backend

#endif   // AMC_BACKEND_BACKEND_H
