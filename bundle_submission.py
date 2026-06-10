#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from pathlib import Path


LOCAL_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')
SYSTEM_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*<([^>]+)>\s*$')


def resolve_local_include(include_name: str, including_file: Path,
                          search_roots: list[Path]) -> Path:
    candidates = [including_file.parent / include_name]
    candidates.extend(root / include_name for root in search_roots)

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    raise FileNotFoundError(
        f'Unable to resolve local include "{include_name}" from {including_file}'
    )


def collect_header_contents(path: Path, search_roots: list[Path],
                            seen_headers: set[Path], system_includes: list[str],
                            seen_system_includes: set[str]) -> list[str]:
    resolved = path.resolve()
    if resolved in seen_headers:
        return []
    seen_headers.add(resolved)

    output: list[str] = [f'// begin {path.as_posix()}']
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        local_match = LOCAL_INCLUDE_RE.match(raw_line)
        if local_match:
            include_path = resolve_local_include(local_match.group(1), path,
                                                search_roots)
            output.extend(
                collect_header_contents(include_path, search_roots, seen_headers,
                                        system_includes, seen_system_includes)
            )
            continue

        system_match = SYSTEM_INCLUDE_RE.match(raw_line)
        if system_match:
            include_text = f'#include <{system_match.group(1)}>'
            if include_text not in seen_system_includes:
                seen_system_includes.add(include_text)
                system_includes.append(include_text)
            continue

        if raw_line.strip() == "#pragma once":
            continue

        output.append(raw_line)

    output.append(f'// end {path.as_posix()}')
    output.append("")
    return output


def collect_cpp_contents(path: Path, search_roots: list[Path],
                         seen_cpps: set[Path], system_includes: list[str],
                         seen_system_includes: set[str]) -> list[str]:
    resolved = path.resolve()
    if resolved in seen_cpps:
        return []
    seen_cpps.add(resolved)

    output: list[str] = [f'// begin {path.as_posix()}']
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        local_match = LOCAL_INCLUDE_RE.match(raw_line)
        if local_match:
            include_path = resolve_local_include(local_match.group(1), path,
                                                search_roots)
            if include_path.suffix == ".h":
                continue
            if include_path.suffix == ".cpp":
                output.extend(
                    collect_cpp_contents(include_path, search_roots, seen_cpps,
                                         system_includes,
                                         seen_system_includes)
                )
            continue

        system_match = SYSTEM_INCLUDE_RE.match(raw_line)
        if system_match:
            include_text = f'#include <{system_match.group(1)}>'
            if include_text not in seen_system_includes:
                seen_system_includes.add(include_text)
                system_includes.append(include_text)
            continue

        output.append(raw_line)

    output.append(f'// end {path.as_posix()}')
    output.append("")
    return output


def build_submission(entry_cpp: Path, output_path: Path) -> None:
    entry_dir = entry_cpp.parent
    source_root = entry_dir.parent if entry_dir.name == "models" else entry_dir
    search_roots = [entry_dir, source_root]

    system_includes: list[str] = []
    seen_system_includes: set[str] = set()
    seen_headers: set[Path] = set()
    seen_cpps: set[Path] = set()

    header_block: list[str] = []
    entry_lines = entry_cpp.read_text(encoding="utf-8").splitlines()
    main_body: list[str] = [f'// begin {entry_cpp.as_posix()}']

    for raw_line in entry_lines:
        local_match = LOCAL_INCLUDE_RE.match(raw_line)
        if local_match:
            include_path = resolve_local_include(local_match.group(1), entry_cpp,
                                                search_roots)
            if include_path.suffix == ".h":
                header_block.extend(
                    collect_header_contents(include_path, search_roots,
                                            seen_headers, system_includes,
                                            seen_system_includes)
                )
            elif include_path.suffix == ".cpp":
                collect_cpp_contents(include_path, search_roots, seen_cpps,
                                     system_includes, seen_system_includes)
            continue

        system_match = SYSTEM_INCLUDE_RE.match(raw_line)
        if system_match:
            include_text = f'#include <{system_match.group(1)}>'
            if include_text not in seen_system_includes:
                seen_system_includes.add(include_text)
                system_includes.append(include_text)
            continue

        main_body.append(raw_line)

    main_body.append(f'// end {entry_cpp.as_posix()}')
    main_body.append("")

    cpp_block: list[str] = []
    for cpp_file in sorted(source_root.glob("*.cpp")):
        if cpp_file.resolve() == entry_cpp.resolve():
            continue
        cpp_block.extend(
            collect_cpp_contents(cpp_file, search_roots, seen_cpps,
                                 system_includes, seen_system_includes)
        )

    output_lines = [
        "// Generated by bundle_submission.py. Do not edit manually.",
        ""
    ]
    output_lines.extend(system_includes)
    output_lines.append("")
    output_lines.extend(header_block)
    output_lines.extend(cpp_block)
    output_lines.extend(main_body)

    output_path.write_text("\n".join(output_lines).rstrip() + "\n",
                           encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Bundle a multi-file contest bot into a single C++ file."
    )
    parser.add_argument("--entry", default="src/models/negamax_noweights.cpp",
                        help="Path to the main translation unit.")
    parser.add_argument("--output", default="submission.cpp",
                        help="Path to write the bundled output.")
    args = parser.parse_args()

    entry_cpp = Path(args.entry).resolve()
    output_path = Path(args.output).resolve()
    build_submission(entry_cpp, output_path)


if __name__ == "__main__":
    main()
