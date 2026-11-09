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
#ifndef AMC_CLI_H
#define AMC_CLI_H

#include <stdint.h>
#include <filesystem>
#include <string>
#include <vector>

#include "SKL/AMC/CLI/Support.h"
#include "SKL/AMC/Util/Error.h"

namespace fs = std::filesystem;

namespace skl::amc::cli {

// The unified error envelope is shared with the other AMC executables.
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

// Top-level commands and aliases. `run` selects one from the argument vector
// after LLVM's CommandLine parser has filled in the per-command options. The
// command-to-spelling table lives in `SKL/AMC/CLI/Reflect.h`.
enum class Command {
    NONE,
    BUILD,
    CONTEXT,
    METADATA,
    ADAPTER,
    QUERY,
    INSPECT,
    VALIDATE,
    PUBLISH,
    FETCH,
    VERIFY,
    TARGET,
    DIFF,
    COMPATIBILITY,
    CACHE,
    GENERATE,
    BACKEND,
    FRONTEND,
};

// Parse the pre-filtered argument vector (without --error-format) and execute
// the selected command. `executable_dir` is where the language providers live.
int run(int argc, char **argv, const fs::path &executable_dir);

int cmd_context();
int cmd_metadata();
int cmd_adapter();
int cmd_query();
int cmd_symbols(bool publish);
int cmd_verify(const fs::path &provider);
int cmd_target();
int cmd_diff(bool compatibility);
int cmd_cache();
int cmd_generate(bool backend, const fs::path &provider);
int cmd_frontend(const fs::path &provider);
int cmd_inspect(bool validate);

}   // namespace skl::amc::cli

#endif   // AMC_CLI_H
