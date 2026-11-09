#pragma once

#include <llvm/Support/CommandLine.h>

#include <string>
#include <vector>

namespace skl::amc::cli {

// Parse `arguments` through LLVM's CommandLine. `arguments[0]` is the program
// name. Returns false and fills `error` when LLVM rejects the command line.
bool parse_command_line(const std::vector<std::string> &arguments, std::string &error);

}   // namespace skl::amc::cli
