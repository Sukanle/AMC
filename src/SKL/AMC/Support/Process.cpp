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
#include "SKL/AMC/Support/Process.h"

#include <atomic>
#include <chrono>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace amc::support {

uint64_t process_id() noexcept {
#if defined(_WIN32)
    return static_cast<uint64_t>(_getpid());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

std::string unique_process_suffix() {
    static std::atomic<uint64_t> sequence{0};
    const uint64_t now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::to_string(process_id())
         + "-"
         + std::to_string(now)
         + "-"
         + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

}   // namespace amc::support
