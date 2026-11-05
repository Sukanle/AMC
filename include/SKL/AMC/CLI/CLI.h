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

namespace amc::cli {

// The unified error envelope is shared with the other AMC executables.
using amc::util::fail;
using amc::util::g_error_format;
using amc::util::read_error_category;
using amc::util::usage_error;

void print_usage();

int cmd_context(char **args, int arg_count);
int cmd_metadata(char **args, int arg_count);
int cmd_adapter(char **args, int arg_count);
int cmd_query(char **args, int arg_count);
int cmd_symbols(char **args, int arg_count);
int cmd_verify(char **args, int arg_count, const fs::path &provider);
int cmd_target(char **args, int arg_count);
int cmd_diff(char **args, int arg_count);
int cmd_cache(char **args, int arg_count);
int cmd_generate(char **args, int arg_count, const fs::path &provider);
int cmd_frontend(char **args, int arg_count, const fs::path &provider);
int cmd_inspect(char **args, int arg_count);

}   // namespace amc::cli

#endif   // AMC_CLI_H
