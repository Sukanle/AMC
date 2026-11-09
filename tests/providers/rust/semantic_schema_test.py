#!/usr/bin/env python3
"""Validate the frozen `amc.rust.semantic/1` adapter schema.

This is a transition-adapter test: v0.3.x does not implement the rustc helper,
but it freezes the document shape the future helper must produce. The validator
is intentionally dependency-free so it also runs on the Windows/macOS CI paths.

Usage:
    semantic_schema_test.py [source_root]
"""

from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

SCHEMA = "amc.rust.semantic/1"
TYPE_KINDS = {"record", "enum", "union", "alias", "primitive", "pointer", "array"}


def require(condition: bool, errors: list[str], path: str, message: str) -> None:
    if not condition:
        errors.append(f"{path}: {message}")


def validate(doc: object) -> list[str]:
    errors: list[str] = []
    require(isinstance(doc, dict), errors, "root", "must be an object")
    if not isinstance(doc, dict):
        return errors

    require(doc.get("schema") == SCHEMA, errors, "schema", f"must be {SCHEMA!r}")

    producer = doc.get("producer")
    require(isinstance(producer, dict), errors, "producer", "must be an object")
    if isinstance(producer, dict):
        require(isinstance(producer.get("kind"), str), errors, "producer.kind", "must be a string")
        require(isinstance(producer.get("version"), str), errors, "producer.version", "must be a string")

    target = doc.get("target")
    require(isinstance(target, dict), errors, "target", "must be an object")
    if isinstance(target, dict):
        pointer_bits = target.get("pointer_bits")
        require(isinstance(pointer_bits, int) and pointer_bits > 0, errors, "target.pointer_bits", "must be a positive integer")
        require(target.get("endianness") in ("little", "big"), errors, "target.endianness", "must be little or big")
        require(isinstance(target.get("triple", ""), str), errors, "target.triple", "must be a string")
        require(isinstance(target.get("c_long_bits", 0), int), errors, "target.c_long_bits", "must be an integer")

    crate = doc.get("crate")
    require(isinstance(crate, dict), errors, "crate", "must be an object")
    if isinstance(crate, dict):
        require(isinstance(crate.get("name"), str) and crate["name"], errors, "crate.name", "must be a non-empty string")
        require(isinstance(crate.get("edition"), str) and crate["edition"], errors, "crate.edition", "must be a non-empty string")

    types = doc.get("types")
    require(isinstance(types, list), errors, "types", "must be an array")
    if isinstance(types, list):
        for index, item in enumerate(types):
            path = f"types[{index}]"
            require(isinstance(item, dict), errors, path, "must be an object")
            if not isinstance(item, dict):
                continue
            require(isinstance(item.get("id"), str) and item["id"], errors, f"{path}.id", "must be a non-empty string")
            require(isinstance(item.get("name"), str) and item["name"], errors, f"{path}.name", "must be a non-empty string")
            require(item.get("kind") in TYPE_KINDS, errors, f"{path}.kind", f"must be one of {sorted(TYPE_KINDS)}")
            require(isinstance(item.get("size"), int) and item["size"] >= 0, errors, f"{path}.size", "must be a non-negative integer")
            require(isinstance(item.get("align"), int) and item["align"] > 0, errors, f"{path}.align", "must be a positive integer")
            require(isinstance(item.get("repr"), list), errors, f"{path}.repr", "must be an array")
            fields = item.get("fields")
            require(isinstance(fields, list), errors, f"{path}.fields", "must be an array")
            if isinstance(fields, list):
                for field_index, field in enumerate(fields):
                    field_path = f"{path}.fields[{field_index}]"
                    require(isinstance(field, dict), errors, field_path, "must be an object")
                    if not isinstance(field, dict):
                        continue
                    require(isinstance(field.get("name"), str) and field["name"], errors, f"{field_path}.name", "must be a non-empty string")
                    require(isinstance(field.get("offset"), int) and field["offset"] >= 0, errors, f"{field_path}.offset", "must be a non-negative integer")
                    require(isinstance(field.get("type"), str) and field["type"], errors, f"{field_path}.type", "must be a non-empty string")

    functions = doc.get("functions")
    require(isinstance(functions, list), errors, "functions", "must be an array")
    if isinstance(functions, list):
        for index, item in enumerate(functions):
            path = f"functions[{index}]"
            require(isinstance(item, dict), errors, path, "must be an object")
            if not isinstance(item, dict):
                continue
            require(isinstance(item.get("name"), str) and item["name"], errors, f"{path}.name", "must be a non-empty string")
            require(isinstance(item.get("symbol"), str) and item["symbol"], errors, f"{path}.symbol", "must be a non-empty string")
            require(isinstance(item.get("abi"), str) and item["abi"], errors, f"{path}.abi", "must be a non-empty string")
            require(isinstance(item.get("return_type"), str) and item["return_type"], errors, f"{path}.return_type", "must be a non-empty string")
            parameters = item.get("parameters")
            require(isinstance(parameters, list), errors, f"{path}.parameters", "must be an array")
            if isinstance(parameters, list):
                for parameter_index, parameter in enumerate(parameters):
                    parameter_path = f"{path}.parameters[{parameter_index}]"
                    require(isinstance(parameter, dict), errors, parameter_path, "must be an object")
                    if not isinstance(parameter, dict):
                        continue
                    require(isinstance(parameter.get("name"), str), errors, f"{parameter_path}.name", "must be a string")
                    require(isinstance(parameter.get("type"), str) and parameter["type"], errors, f"{parameter_path}.type", "must be a non-empty string")
                    if "mode" in parameter:
                        require(parameter["mode"] in ("borrowed", "owned", "copy", "unspecified"), errors,
                                f"{parameter_path}.mode", "must be a known ownership mode")

    for optional in ("generics", "traits", "errors", "lifetimes", "trait_objects", "async_functions"):
        if optional in doc:
            require(isinstance(doc[optional], list), errors, optional, "must be an array when present")

    return errors


def check(name: str, doc: object, should_pass: bool) -> int:
    errors = validate(doc)
    ok = not errors if should_pass else bool(errors)
    if ok:
        print(f"  [PASS] {name}")
        return 0
    print(f"  [FAIL] {name}: {errors[:3]}")
    return 1


def main() -> int:
    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[3]
    fixture = source_root / "tests" / "fixtures" / "rust" / "semantics" / "rust_semantic_v1.json"
    try:
        sample = json.loads(fixture.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"  [FAIL] cannot load {fixture}: {error}", file=sys.stderr)
        return 1

    failures = 0
    failures += check("sample document is valid", sample, True)

    extra = copy.deepcopy(sample)
    extra["extra_optional"] = {"ignored": True}
    failures += check("unknown optional fields are ignored", extra, True)

    missing_name = copy.deepcopy(sample)
    del missing_name["types"][0]["name"]
    failures += check("missing type name is rejected", missing_name, False)

    missing_target = copy.deepcopy(sample)
    del missing_target["target"]
    failures += check("missing target is rejected", missing_target, False)

    bad_schema = copy.deepcopy(sample)
    bad_schema["schema"] = "amc.rust.semantic/2"
    failures += check("wrong schema version is rejected", bad_schema, False)

    missing_abi = copy.deepcopy(sample)
    del missing_abi["functions"][0]["abi"]
    failures += check("missing function ABI is rejected", missing_abi, False)

    if failures:
        print(f"  [FAIL] {failures} schema check(s) failed")
        return 1
    print("  [PASS] amc.rust.semantic/1 schema is frozen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
