#pragma once

#include <llvm/Support/CommandLine.h>

namespace skl::amc::support {

// One shared category for every AMC option. It lives in a header-level
// function so the CLI and the compilation layer can register options without
// depending on each other, and so static initialization order is well defined.
inline llvm::cl::OptionCategory &cli_category() {
    static llvm::cl::OptionCategory category("AMC Options");
    return category;
}

}   // namespace skl::amc::support
