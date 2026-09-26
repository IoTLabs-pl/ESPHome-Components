#!/usr/bin/env python3
"""Runs the XMQ generator over drivers/src and groups the failures by construct.

Needs esphome importable: run it from the esphome venv.
"""

import argparse
import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
COMPONENTS_DIR = REPO_ROOT / "components"
DRIVERS_DIR = COMPONENTS_DIR / "wmbus_common" / "drivers" / "src"

sys.path.insert(0, str(COMPONENTS_DIR))

from wmbus_common.drivers.loader.driver import Driver  # noqa: E402


def describe(error: Exception) -> set[str]:
    """voluptuous paths without list indices, so one construct groups across fields."""
    # MultipleInvalid carries every violation, not just the first.
    errors = getattr(error, "errors", None) or [error]

    described = set()
    for item in errors:
        path = getattr(item, "path", None)
        if path:
            readable = ".".join(str(p) for p in path if not isinstance(p, int))
            described.add(f"unsupported: {readable}")
        else:
            described.add(f"{type(item).__name__}: {item}")

    return described


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="list every driver, not just the failures",
    )
    args = parser.parse_args()

    paths = sorted(DRIVERS_DIR.glob("*.xmq"))

    generated: list[tuple[str, int]] = []
    failures: dict[str, list[str]] = defaultdict(list)

    for path in paths:
        driver = Driver.from_source(path)
        try:
            driver.serialize()
        except Exception as error:  # noqa: BLE001 - reporting tool
            for reason in describe(error):
                failures[reason].append(path.stem)
            continue
        generated.append((path.stem, len(driver.fields)))

    if args.verbose:
        for name, fields in generated:
            print(f"  ok    {name:24s} {fields:3d} fields")

    for reason, names in sorted(failures.items(), key=lambda kv: -len(kv[1])):
        print(f"  FAIL  {reason}")
        print(f"          {len(names)}: {', '.join(names)}")

    total = len(paths)
    print(
        f"\n{len(generated)}/{total} drivers generated, "
        f"{sum(f for _, f in generated)} fields, "
        f"{total - len(generated)} failing"
    )

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
