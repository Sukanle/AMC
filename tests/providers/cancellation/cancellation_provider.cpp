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
// Test-only provider used by the cooperative-cancellation IPC test.
//
// The capability polls `skl::amc::lang::cancellation_requested()` at a safe point.
// A CANCEL must therefore turn the long-running projection into
// `kCapabilityCancelled` instead of waiting for the loop to finish.
#include "SKL/AMC/Lang/Lang.h"

#include <chrono>
#include <thread>

namespace {

int slow_backend(const char *, const char *) {
    for (int i = 0; i < 5'000; ++i) {
        if (skl::amc::lang::cancellation_requested()) return skl::amc::lang::kCapabilityCancelled;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

}   // namespace

int main(int argc, char **argv) {
    static const skl::amc::lang::Provider provider{
        "cancel",
        {
          {"backend", slow_backend},
          },
        "0.0.0",
        {"test"},
    };
    return skl::amc::lang::run_provider(argc, argv, provider);
}
