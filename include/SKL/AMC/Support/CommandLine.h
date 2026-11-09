#pragma once

#include "SKL/AMC/Support/Command.h"
#include "SKL/AMC/Util/Error.h"

#include <llvm/Support/CommandLine.h>

#include <string>
#include <string_view>

namespace skl::amc::support {

// One shared category for every AMC option. It lives in a header-level
// function so the CLI and the compilation layer can register options without
// depending on each other, and so static initialization order is well defined.
inline llvm::cl::OptionCategory &cli_category() {
    static llvm::cl::OptionCategory category("AMC Options");
    return category;
}

// Applied by the `--error-format` callback. The callback is the single
// authority for the error envelope, so it wins over `--format json` regardless
// of argument order.
inline void apply_error_format(std::string_view value) {
    amc::ErrorFormat format;
    if (amc::parse_error_format(value, format)) {
        amc::util::g_error_format = format;
        amc::util::g_error_format_explicit = true;
    } else {
        amc::util::g_error_format_invalid = true;
    }
}

// Applied by every `--format` callback. A command's JSON output implies a JSON
// error envelope unless the user explicitly selected `--error-format`.
inline void note_output_format(std::string_view value) {
    if (!amc::util::g_error_format_explicit && value == "json") amc::util::g_error_format = amc::ErrorFormat::JSON;
}

inline llvm::cl::cb<void, std::string> error_format_callback() {
    return llvm::cl::cb<void, std::string>([](std::string value) { apply_error_format(value); });
}

inline llvm::cl::cb<void, std::string> format_callback() {
    return llvm::cl::cb<void, std::string>([](std::string value) { note_output_format(value); });
}

}   // namespace skl::amc::support
