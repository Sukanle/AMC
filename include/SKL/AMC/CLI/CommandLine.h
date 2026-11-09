#pragma once

#include <llvm/Support/CommandLine.h>

#include <string>

namespace skl::amc::cli {

// Parse `argv` through LLVM's CommandLine. Returns false and fills `error` when
// LLVM rejects the command line.
bool parse_command_line(int argc, char **argv, std::string &error);

}   // namespace skl::amc::cli
