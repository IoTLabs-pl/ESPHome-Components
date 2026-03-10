#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path


def run_format(
    args: argparse.Namespace,
    path: Path,
) -> tuple[str, bool]:
    """Run clang-format on a single file. Returns (path, is_failed)."""
    invocation = ["clang-format"]
    if args.inplace:
        invocation.append("-i")
    else:
        invocation.extend(["-n", "--Werror"])
    invocation.append(str(path))

    proc = subprocess.run(
        invocation,
        capture_output=True,
        encoding="utf-8",
        check=False,
    )

    if proc.returncode != 0:
        formatted_content = subprocess.run(
            ["clang-format", path],
            capture_output=True,
            encoding="utf-8",
        ).stdout

        diff = subprocess.run(
            ["diff", "--color=always", "-u", path, "-"],
            input=formatted_content,
            capture_output=True,
            encoding="utf-8",
        ).stdout
    else:
        diff = None

    return (path, diff)


def find_files() -> list[Path]:
    """Find all .cpp and .h files in components directory."""
    components_dir = Path(__file__).parent.parent / "components"

    return sorted(
        path
        for pattern in ("**/*.cpp", "**/*.h")
        for path in components_dir.glob(pattern)
        # Only allow component.cpp and component.h in wmbus_common
        if ("wmbus_common" not in path.parts or path.stem == "component")
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check code formatting with clang-format"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="number of format instances to be run in parallel",
    )
    parser.add_argument(
        "-i",
        "--inplace",
        action="store_true",
        help="reformat files in-place",
    )
    args = parser.parse_args()

    files = find_files()

    print(f"Checking {len(files)} files with {args.jobs} workers...")

    failed_files: list[tuple[Path,str]] = []

    # Use ThreadPoolExecutor for parallel execution
    with ProcessPoolExecutor(max_workers=args.jobs) as executor:
        # Submit all tasks
        futures = {
            executor.submit(run_format, args, file_path): file_path
            for file_path in files
        }

        # Process results as they complete
        for future in as_completed(futures):
            path, diff = future.result()
            if diff:
                failed_files.append((path, diff))

    # Generate diffs for failed files
    for path, diff in failed_files:
        print(f"❌ Bad formatting in: {path}")
        print(f"== DIFF for {path} ==")
        print(diff)

    if failed_files:
        print(f"\n❌ {len(failed_files)} file(s) with formatting issues:")
        for f, _ in failed_files:
            print(f"  - {f}")
        return 1
    else:
        print(f"✅ All {len(files)} files are properly formatted")
        return 0


if __name__ == "__main__":
    sys.exit(main())
