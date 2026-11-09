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
#include "SKL/AMC/Util/Error.h"

#include <fmt/format.h>

namespace skl::amc::util {

amc::ErrorFormat g_error_format = amc::ErrorFormat::TEXT;
bool g_error_format_explicit = false;
bool g_error_format_invalid = false;

int fail(amc::ErrorCategory category, const std::string &stage, const std::string &message, const std::string &file,
    const std::string &detail) {
    const auto error = amc::make_error(category, stage, message, file, detail);
    fmt::print(stderr, "{}\n",
        g_error_format == amc::ErrorFormat::JSON ? amc::error_to_json(error) : amc::error_to_text(error));
    return 1;
}

int usage_error(const std::string &message) {
    fail(amc::ErrorCategory::USAGE, "options", message);
    return 2;
}

amc::ErrorCategory read_error_category(const std::string &message) {
    if (message.rfind("cannot open", 0) == 0) return amc::ErrorCategory::IO;
    return amc::ErrorCategory::FORMAT;
}

}   // namespace skl::amc::util
