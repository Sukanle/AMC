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
#include "SKL/AMC/Error.h"

#ifdef AMC_HAS_EPCD_INTERFACE
#  include "SKL/EPCD/Interface/CategoryRegistry.h"
#endif

#include <cstddef>
#include <cstdio>

namespace amc {
namespace {

#ifdef AMC_HAS_EPCD_INTERFACE
constexpr std::size_t epcd_registry_capacity = 8;
using epcd_registry_type = ::skl::epcd::iface::CategoryRegistry<epcd_registry_capacity>;

epcd_registry_type &registry() noexcept {
    static epcd_registry_type instance;
    return instance;
}

::skl::epcd::iface::CategoryStatus registration_status() noexcept {
    static const auto status = registry().add_static(AMCErr_domain);
    return status;
}

bool registration_available(::skl::epcd::iface::CategoryStatus status) noexcept {
    return status == ::skl::epcd::iface::CategoryStatus::ok
        || status == ::skl::epcd::iface::CategoryStatus::duplicate_domain;
}
#endif

}   // namespace

std::string error_message(const ::skl::epcd::ErrorCode &code) {
    if (code.ok()) return "success";

    if (code.domain == AMCErr_domain.descriptor.domain) {
        if (const char *text = ::skl::epcd::message(AMCErr_domain, code.value); text != nullptr) return text;
    }
    if (code.domain == ::skl::epcd::domain::std_compat) {
        return std::make_error_code(static_cast<std::errc>(code.value)).message();
    }

    char buffer[96] = {};
    std::snprintf(buffer, sizeof(buffer), "error domain 0x%016llx code %d",
        static_cast<unsigned long long>(code.domain), static_cast<int>(code.value));
    return buffer;
}

bool register_errors() noexcept {
#ifdef AMC_HAS_EPCD_INTERFACE
    return registration_available(registration_status());
#else
    // Core-only builds still expose the immutable static domain; dynamic EPCD
    // registration requires the optional Interface component.
    return false;
#endif
}

bool errors_registered() noexcept {
#ifdef AMC_HAS_EPCD_INTERFACE
    return registration_available(registration_status());
#else
    return false;
#endif
}

#ifdef AMC_HAS_EPCD_INTERFACE
[[maybe_unused]] const bool amc_epcd_registration = register_errors();
#endif

}   // namespace amc
