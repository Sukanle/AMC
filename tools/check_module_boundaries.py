#!/usr/bin/env python3
"""Report AMC module-boundary dependencies.

A0/A1 migration aid. The default mode is report-only: it prints the current
dependency edges, forbidden directions and compatibility forwarding headers,
but never fails the build. ``--strict`` turns violations into a non-zero exit;
``--contracts-only`` checks only the frozen A0 contract headers and is used by
CTest as a guard against new forbidden includes in those headers.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

CONTRACT_HEADERS = (
    "include/SKL/AMC/IR/Module.h",
    "include/SKL/AMC/Diagnostics/Diagnostics.h",
    "include/SKL/AMC/Frontend/Frontend.h",
    "include/SKL/AMC/Pass/Pass.h",
    "include/SKL/AMC/Backend/Backend.h",
    "include/SKL/AMC/Compilation/Compilation.h",
)

# Forbidden includes for the frozen contract headers. Compilation is allowed to
# name Backend and Frontend; IR/Pass/Backend/Frontend are not.
CONTRACT_FORBIDDEN_BASE = ("clang/", "llvm/", "SKL/AMC/Lang/C++", "SKL/AMC/CLI/", "SKL/AMC/Tool/")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# Allowed dependency directions for the target modules. A report-only run lists
# the legacy tree's violations; --strict can be enabled once A1 migration ends.
ALLOWED = {
    "IR": {"IR", "Diagnostics", "Support"},
    "Frontend": {"IR", "Diagnostics", "Support", "Frontend"},
    "Pass": {"IR", "Diagnostics", "Support", "Pass"},
    "Backend": {"IR", "Diagnostics", "Support", "Backend", "Artifact"},
    "Compilation": {"IR", "Diagnostics", "Frontend", "Pass", "Backend", "Support", "Compilation", "Artifact"},
    "Interfaces": {"Compilation", "Diagnostics", "Support", "Interfaces"},
    "Provider": {"Frontend", "Backend", "Diagnostics", "Support", "Provider"},
    "Platform": {"IR", "Diagnostics", "Support", "Platform"},
    "Artifact": {"IR", "Diagnostics", "Support", "Artifact", "Platform"},
    "Query": {"IR", "Diagnostics", "Support", "Query", "Artifact", "Platform"},
    "Support": {"Support", "IR", "Artifact", "Diagnostics"},
    "Other": None,
}


def module_for_path(path: Path, root: Path) -> str:
    rel = path.resolve().relative_to(root.resolve()).as_posix()
    if "/include/SKL/AMC/IR/" in rel or "/src/SKL/AMC/IR/" in rel or "/Semantic/" in rel:
        return "IR"
    if "/include/SKL/AMC/Frontend/" in rel or "/src/SKL/AMC/Frontend/" in rel:
        return "Frontend"
    if "/include/SKL/AMC/Backend/" in rel or "/src/SKL/AMC/Backend/" in rel:
        return "Backend"
    if "/Pass/" in rel:
        return "Pass"
    if "/Compilation/" in rel:
        return "Compilation"
    if "/Diagnostics/" in rel:
        return "Support"
    if "/Core/Verify" in rel:
        return "Pass"
    if "/Core/Cache" in rel:
        return "Compilation"
    if "/Core/ELF" in rel or "/Platform/" in rel:
        return "Platform"
    if "/Core/Metadata" in rel or "/Artifact/" in rel or "/Serialization/" in rel:
        return "Artifact"
    if "/Core/Query" in rel or "/Core/Context" in rel or "/Tool/Dump/" in rel or "/Query/" in rel:
        return "Query"
    if "/Support/" in rel:
        return "Support"
    if "/Lang/C++/C++.cpp" in rel or "/Lang/Rust/Rust.cpp" in rel or "/Lang/Zig/Zig.cpp" in rel:
        return "Provider"
    if "/Lang/" in rel:
        return "Frontend"
    if "/Tool/Dump/" in rel or "/Query/" in rel:
        return "Query"
    if "/CLI/" in rel:
        cli_name = Path(rel).name
        if cli_name in {"Support.cpp", "Support.h"}:
            return "Support"
        if cli_name in {"Query.cpp", "Context.cpp", "Metadata.cpp", "Symbols.cpp"}:
            return "Query"
        if cli_name in {"Verify.cpp", "Diff.cpp", "Adapter.cpp", "Generate.cpp", "Cache.cpp"}:
            return "Compilation"
        return "Interfaces"
    if "/Tool/MCP/" in rel or "/Interfaces/" in rel or rel.endswith("main.cpp"):
        return "Interfaces"
    if "/Core/" in rel:
        return "IR"
    return "Other"


def module_for_include(include: str) -> str | None:
    if not include.startswith("SKL/AMC/"):
        return None
    if include.startswith("SKL/AMC/IR/") or include.startswith("SKL/AMC/Semantic/"):
        return "IR"
    if include.startswith("SKL/AMC/Frontend/"):
        return "Frontend"
    if include.startswith("SKL/AMC/Backend/"):
        return "Backend"
    if include.startswith("SKL/AMC/Pass/"):
        return "Pass"
    if include.startswith("SKL/AMC/Compilation/"):
        return "Compilation"
    if include.startswith("SKL/AMC/Diagnostics/"):
        return "Support"
    if include.startswith("SKL/AMC/Artifact/"):
        return "Artifact"
    if include.startswith("SKL/AMC/Platform/"):
        return "Platform"
    if include.startswith("SKL/AMC/Query/"):
        return "Query"
    if include.startswith("SKL/AMC/Interfaces/"):
        return "Interfaces"
    if include.startswith("SKL/AMC/Support/"):
        return "Support"
    if include.startswith("SKL/AMC/Lang/"):
        return "Frontend"
    if include.startswith("SKL/AMC/Core/") or include.startswith("SKL/AMC/Platform/"):
        return "IR"
    if include.startswith("SKL/AMC/Tool/Dump"):
        return "Query"
    if include.startswith("SKL/AMC/Tool/MCP"):
        return "Interfaces"
    if include == "SKL/AMC/CLI/Support.h":
        return "Support"
    if include.startswith("SKL/AMC/CLI/") or include.startswith("SKL/AMC/Tool/"):
        return "Interfaces"
    return None


def source_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for base in ("include", "src"):
        directory = root / base
        if not directory.is_dir():
            continue
        files.extend(
            path
            for path in directory.rglob("*")
            if path.suffix in {".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx"}
        )
    return files


def compat_forwarders(root: Path) -> dict[Path, list[Path]]:
    result: dict[Path, list[Path]] = {}
    all_files = source_files(root)
    for path in all_files:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if "AMC_COMPAT_FORWARD" not in text:
            continue
        # The include spelling is derived from the include root; keep it simple
        # and report every source that includes this header by its repo-relative
        # suffix.
        rel = path.resolve().relative_to(REPO_ROOT).as_posix()
        suffix = rel.split("include/", 1)[-1] if "include/" in rel else rel
        users = []
        for candidate in all_files:
            if candidate == path:
                continue
            try:
                candidate_text = candidate.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            if f'#include "{suffix}"' in candidate_text or f"#include <{suffix}>" in candidate_text:
                users.append(candidate.resolve().relative_to(REPO_ROOT))
        result[path.resolve().relative_to(REPO_ROOT)] = sorted(users)
    return result


def load_allowlist(root: Path) -> list[tuple[str, str, str, str, str]]:
    path = root / "tools" / "module_boundary_allowlist.txt"
    if not path.is_file():
        return []
    entries: list[tuple[str, str, str, str, str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("|")]
        if len(parts) < 4:
            continue
        file, source, target, until = parts[:4]
        reason = parts[4] if len(parts) > 4 else ""
        entries.append((file, source, target, until, reason))
    return entries


def check_contracts(root: Path) -> list[str]:
    errors: list[str] = []
    for rel in CONTRACT_HEADERS:
        path = root / rel
        if not path.is_file():
            errors.append(f"missing contract header: {rel}")
            continue
        forbidden = list(CONTRACT_FORBIDDEN_BASE)
        if not rel.startswith("include/SKL/AMC/Compilation/"):
            forbidden.append("SKL/AMC/Backend/")
        if not rel.startswith("include/SKL/AMC/Compilation/") and not rel.startswith("include/SKL/AMC/Frontend/"):
            forbidden.append("SKL/AMC/Frontend/")
        text = path.read_text(encoding="utf-8", errors="replace")
        for include in INCLUDE_RE.findall(text):
            for item in forbidden:
                if item in include:
                    errors.append(f"{rel} includes forbidden header: {include}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Report AMC module-boundary dependencies")
    parser.add_argument("--root", type=Path, default=REPO_ROOT)
    parser.add_argument("--strict", action="store_true", help="fail on forbidden edges")
    parser.add_argument("--contracts-only", action="store_true", help="check only the A0 contract headers")
    parser.add_argument("--json", action="store_true", help="emit a JSON report")
    args = parser.parse_args()
    root = args.root.resolve()

    if args.contracts_only:
        errors = check_contracts(root)
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1
        print("module boundaries: A0 contract headers are clean")
        return 0

    allowlist = load_allowlist(root)
    allowlist_used = {key: False for key in allowlist}
    edges: list[tuple[str, str, str]] = []
    violations: list[tuple[str, str, str]] = []
    for path in source_files(root):
        source_module = module_for_path(path, root)
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        is_compat_forwarder = "AMC_COMPAT_FORWARD" in text
        for include in INCLUDE_RE.findall(text):
            target_module = module_for_include(include)
            if target_module is None:
                continue
            rel = path.resolve().relative_to(REPO_ROOT).as_posix()
            edges.append((rel, source_module, target_module))
            allowed = ALLOWED.get(source_module)
            if not is_compat_forwarder and allowed is not None and target_module not in allowed:
                matched = None
                for entry in allowlist:
                    file, source, target, until, reason = entry
                    if file == rel and source == source_module and target == target_module:
                        matched = entry
                        allowlist_used[entry] = True
                        break
                if matched is None:
                    violations.append((rel, source_module, target_module))

    forwarders = compat_forwarders(root)
    if args.json:
        import json

        print(
            json.dumps(
                {
                    "edges": len(edges),
                    "violations": [
                        {"file": file, "from": source, "to": target}
                        for file, source, target in sorted(set(violations))
                    ],
                    "compat_forwarders": {
                        str(path): [str(user) for user in users] for path, users in forwarders.items()
                    },
                },
                indent=2,
            )
        )
    else:
        print(f"AMC module boundary report ({len(edges)} SKL/AMC include edges)")
        if forwarders:
            print("\nCompatibility forwarding headers:")
            for path, users in forwarders.items():
                print(f"  {path}")
                if users:
                    for user in users:
                        print(f"    used by {user}")
                else:
                    print("    (no include sites; remove it)")
        else:
            print("\nCompatibility forwarding headers: none")
        unique = sorted(set(violations))
        if unique:
            print(f"\nForbidden dependency edges ({len(unique)}):")
            for file, source, target in unique:
                print(f"  {file}: {source} -> {target}")
        else:
            print("\nNo forbidden dependency edges.")
        temporary = [entry for entry in allowlist if allowlist_used.get(entry)]
        if temporary:
            print(f"\nTemporary allowlisted edges ({len(temporary)}):")
            for file, source, target, until, reason in temporary:
                print(f"  {file}: {source} -> {target} [until {until}] {reason}")
        stale = [entry for entry in allowlist if not allowlist_used.get(entry)]
        if stale:
            print(f"\nStale allowlist entries ({len(stale)}):")
            for file, source, target, until, reason in stale:
                print(f"  {file}: {source} -> {target} [until {until}] {reason}")

    if args.strict and violations:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
