#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
amc="${AMC:-amc}"

cd "$here"
mkdir -p provider/src/generated consumer/src/generated target/amc

"$amc" build -c amc.abic.toml -B target/amc
cargo build -p amc_demo_provider --release
"$amc" generate target/amc/amc_demo.abix -l rust -o consumer/src/generated

provider_dir="$here/target/release"
AMC_PROVIDER_DIR="$provider_dir" \
    LD_LIBRARY_PATH="$provider_dir:${LD_LIBRARY_PATH:-}" \
    DYLD_LIBRARY_PATH="$provider_dir:${DYLD_LIBRARY_PATH:-}" \
    cargo run -p amc_demo_consumer --release
