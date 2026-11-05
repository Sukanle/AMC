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
#ifndef AMC_ERROR_IDENTITY_H
#define AMC_ERROR_IDENTITY_H

#include "SKL/EPCD/Core/StaticDomain.h"

#include <string>
#include <system_error>

namespace amc {

// AMC owns this static error domain. Values are stable and must never be
// renumbered once released.
#define AMC_ERROR_LIST(X)                                                                                        \
    X(success, 0x0000, "Success.", ::skl::epcd::GenericCondition::none)                                          \
    X(usage, 0x0001, "Invalid command-line usage.", ::skl::epcd::GenericCondition::invalid_argument)             \
    X(config, 0x0002, "Configuration could not be parsed.", ::skl::epcd::GenericCondition::invalid_argument)     \
    X(io, 0x0003, "I/O operation failed.", ::skl::epcd::GenericCondition::io_error)                              \
    X(provider, 0x0004, "Language provider failed.", ::skl::epcd::GenericCondition::protocol_error)              \
    X(format, 0x0005, "Artifact format is invalid.", ::skl::epcd::GenericCondition::invalid_argument)            \
    X(validation, 0x0006, "Artifact validation failed.", ::skl::epcd::GenericCondition::invalid_argument)        \
    X(compatibility, 0x0007, "ABI compatibility check failed.", ::skl::epcd::GenericCondition::invalid_argument) \
    X(internal, 0x0008, "Internal AMC error.", ::skl::epcd::GenericCondition::unknown)

EPCD_DEFINE_STATIC_DOMAIN(AMCErr, ::skl::epcd::make_skl_domain(0x003, 0x0001), "AMC", AMC_ERROR_LIST);

// Compatibility spelling for AMC call sites.
using errc = AMCErr;

// Converts an OS/generic error to EPCD's built-in std-compatibility domain.
// The original numeric value is preserved; the AMC public API never exposes a
// raw std::error_code.
[[nodiscard]] constexpr skl::epcd::ErrorCode from_std_errc(std::errc value) noexcept {
    return skl::epcd::make_error_code(skl::epcd::domain::std_compat, static_cast<skl::epcd::ErrorValue>(value));
}

// Human-readable fallback text for an EPCD code. AMC-domain codes use the
// static table; std-compat codes use the platform's generic message.
[[nodiscard]] std::string error_message(const skl::epcd::ErrorCode &code);

// Registers the AMC static domain with EPCD's CategoryRegistry. Idempotent;
// also runs automatically when the library is loaded.
[[nodiscard]] bool register_errors() noexcept;
[[nodiscard]] bool errors_registered() noexcept;

}   // namespace amc

#endif   // AMC_ERROR_IDENTITY_H
