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
// C++ interop consumer for the same native library used by the Rust and Zig
// consumers. It includes the public C ABI and the AMC-generated C++ projection,
// so the generated static assertions run before main().
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "demo_api.hpp"
#include "amc_generated.hpp"

extern "C" double scale_adapter(const void *object, double factor) { return demo_scale_handle(object, factor); }

int main() {
    assert(demo_add(2, 3) == 5);

    DemoVec2 value{4, 2.5};
    assert(demo_scale(&value, 2.0) == 13.0);

    // Bind the same handle-shaped interface from the shared artifact.
    ::skl::abix::poly::InterfaceMethod scale_methods[] = {
        {amc_generated::IVectorScaleTraits::signatures[0], ::skl::abix::poly::interface_entry(&scale_adapter), 0, 0}
    };
    amc_generated::IVectorScale scale;
    assert(scale.bind_methods(amc_generated::IVectorScaleTraits::id, amc_generated::IVectorScaleTraits::version,
               scale_methods, 1, &value)
           == ::skl::abix::poly::InterfaceStatus::ok);
    assert(scale.demo_scale_handle(2.0) == 13.0);

    DemoVec2 *owned = demo_vec2_create(7, 1.5);
    assert(owned != nullptr);
    assert(owned->x == 7);
    assert(demo_live_count() == 1);
    assert(demo_vec2_destroy(owned) == 0);
    assert(demo_live_count() == 0);

    int32_t out = 0;
    assert(demo_div(10, 2, &out) == 0);
    assert(out == 5);
    assert(demo_div(10, 0, &out) != 0);

    const abix_span_i32 numbers = demo_numbers();
    assert(numbers.len == 5);
    assert(numbers.data[0] == 1);
    assert(demo_sum(numbers) == 15);

    printf("cpp interop ok\n");
    return 0;
}
