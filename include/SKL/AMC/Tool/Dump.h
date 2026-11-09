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
#ifndef AMC_DUMP_H
#define AMC_DUMP_H

#include <string>

#include "SKL/AMC/Core/Core.h"

namespace skl::amc::dump {

void write_json(const amc::ABIModule &module, std::string &output);
void write_text(const amc::ABIModule &module, std::string &output);
void write_diff_text(
    const amc::ABIModule &source, const amc::ABIModule &target, const amc::ABIModule &report, std::string &output);
void write_diff_json(
    const amc::ABIModule &source, const amc::ABIModule &target, const amc::ABIModule &report, std::string &output);

}   // namespace skl::amc::dump

#endif   // AMC_DUMP_H
