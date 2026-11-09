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
#ifndef AMC_CONTEXT_H
#define AMC_CONTEXT_H

#include "SKL/AMC/Core/Core.h"

#include <llvm/Support/raw_ostream.h>

#include <string>

namespace skl::amc {

// AI-P1: render an ABI module as an LLM-friendly context document.
//
// The `llm` format is a deterministic, token-efficient outline: types are
// indexed as T0..Tn, fields as F0..Fn and functions as P0..Pn so that every
// cross reference is a short index instead of a repeated 32-hex-digit hash.
// The authoritative hashes are still printed once on the owning record, so
// the document stays self-contained and precise.
//
// `include_names` mirrors `amc dump --no-names`: when false, semantic names
// are omitted (useful once `.abix.names` has been stripped) while indices and
// hashes keep the context fully resolvable.
// Stream the context document directly to `out`. These overloads avoid
// materializing the complete document in memory; the string versions above are
// thin wrappers for callers that need a single value.
void context_to_llm(const ABIModule &module, bool include_names, llvm::raw_ostream &out);
void context_to_json(const ABIModule &module, bool include_names, llvm::raw_ostream &out);

std::string context_to_llm(const ABIModule &module, bool include_names);
std::string context_to_json(const ABIModule &module, bool include_names);

}   // namespace skl::amc

#endif
