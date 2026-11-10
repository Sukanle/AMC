// Compatibility forwarding header for the interop fixture.
//
// The Host registry lives in the public AMC runtime now so the same
// implementation serves the compiler, the lease fixture and generated
// LeaseGuard consumers.
#pragma once

#include "SKL/AMC/Support/LeaseRegistry.h"

namespace skl::amc::test::lease {

using HostLeaseRegistry = skl::amc::support::LeaseRegistry;

}   // namespace skl::amc::test::lease
