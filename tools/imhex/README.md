# ImHex patterns for AMC binaries

These [ImHex Pattern Language](https://docs.werwolv.net/imhex/patterns) files describe the binary images AMC writes:

| Pattern                | Input                | Purpose                                                   |
| ---------------------- | -------------------- | --------------------------------------------------------- |
| `abix_artifact.hexpat` | `.abix` artifact     | ABIX v4 header, section directory and every known section |
| `abix_metadata.hexpat` | ABIX Metadata Region | embedded `.abix.metadata` / `AMC_METADATA_REGION` bytes   |

JSON sidecars (`.abix.meta`, `.abix.semantic`, `.abix.lowering`) are text and should be opened as JSON, not with a binary pattern.

## Using the patterns

1. Open the binary in ImHex.
2. `File → Import → Pattern` (or drag the `.hexpat` file onto the pattern editor), then run the pattern.

`abix_artifact.hexpat` expects the ABIX artifact at file offset `0`. It resolves the section directory and displays the type, field, function, parameter, symbol, ownership, error, interface and ABIX-STD lowering tables, including names from the string table.

`abix_metadata.hexpat` expects a Metadata Region at file offset `0`. For a region embedded in an ELF/PE/Mach-O binary, set `metadata_base` to the section offset:

```cpp
u64 metadata_base = 0x1234;   // .abix.metadata / .abix.names file offset
```

The pattern then parses the manifest, the desc tables, the optional hash index, the extension directory (ownership/error/target/lowering/view/range/interface/ layout-encoding) and the names table.

## Test artifacts

`python tools/run.py` keeps the binaries produced during the test stage under `build/tmp` by default. Each test gets its own temporary subtree, for example:

```text
build/tmp/amc-rust.xxxx/build/rust_demo.abix
build/tmp/amc-interop.xxxx/libdemo.so
build/tmp/amc-rust-panic.xxxx/libpanic_provider.so
```

Use `--artifact-dir <dir>` to change the location. The same directory is exported as `AMC_ARTIFACT_DIR`, and `TMPDIR`/`TMP`/`TEMP` are pointed at it so tests that create temporary trees keep their AMC outputs there.
