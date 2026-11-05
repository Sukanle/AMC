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
#include "SKL/AMC/CLI/CLI.h"

#include <string>

#include <fmt/format.h>

namespace amc::cli {

void print_usage() {
    fmt::print(stderr,
        "usage:\n"
        "  amc build -c <file>.abic.toml [-B <build_dir>] [--cache <dir>] [--deps <files>]\n"
        "  amc generate <file>.abix -l cpp|lua|rust|zig -o <directory|file>\n"
        "  amc validate <file>.abix\n"
        "  amc inspect <file>.abix\n"
        "  amc context <file>.abix [--format llm|json] [--no-names] [-o <file>]\n"
        "  amc query <file>.abix --type|--function <name> [--layout] [--format text|json]\n"
        "  amc query <file>.abix --compatible <other.abix> [--format text|json]\n"
        "  amc adapter <source.abix> <target.abix> [-o <file>] [--typed] [--shim]\n"
        "  amc publish <file.abix|binary> [--root <dir>] [--build-id <hex>]\n"
        "  amc fetch <binary>|--build-id <hex> [--root <dir>] [-o <file>]\n"
        "  amc metadata <file>.abix [--format bin|meta|json] [-o <file>]\n"
        "                            [--no-names] [--no-hash-index] [--build-id <hex>]\n"
        "  amc metadata --verify <region> [--format text|json]\n"
        "  amc metadata --from-elf <binary> [--format bin|json] [-o <file>]\n"
        "  amc verify <contract.abix> <implementation.abix> [--format text|json]\n"
        "  amc verify -c <file>.abic.toml [-B <build_dir>] [--format text|json]\n"
        "  amc target <file.abix|binary> [--expect arch=..,os=..,pointer_bits=..,endianness=..] [--format text|json]\n"
        "  amc diff <source.abix> <target.abix> [-o report.abix]\n"
        "  amc compatibility <source.abix> <target.abix>\n"
        "  amc cache <dir> [--verify|--clear] [--format text|json]\n"
        "global options:\n"
        "  --error-format text|json   structured error envelope (default text)\n");
}

}   // namespace amc::cli
