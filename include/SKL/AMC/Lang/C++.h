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
#pragma once

// AMC_COMPAT_FORWARD
//
// A1 compatibility forwarder for the C++ provider split. The implementation
// lives in Frontend/Cpp and Backend/Cpp; this header stays only for include
// sites that have not migrated yet. Remove it once
// tools/check_module_boundaries.py reports no users.
//
// Target: SKL/AMC/Frontend/Cpp/Cpp.h + SKL/AMC/Backend/Cpp/Cpp.h
#include "SKL/AMC/Backend/Cpp/Cpp.h"
#include "SKL/AMC/Frontend/Cpp/Cpp.h"
