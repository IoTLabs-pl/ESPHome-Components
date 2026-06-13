#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path

# Resolve project root (parent of script directory) once
parent_dir = Path(__file__).resolve().parents[2]


def run(cmd):
    result = subprocess.run(cmd, shell=True, cwd=parent_dir)
    if result.returncode != 0:
        sys.exit(result.returncode)


def main():
    parser = argparse.ArgumentParser(
        description="Pull a subtree from wmbusmeters/wmbusmeters repo."
    )
    parser.add_argument("tag", help="Tag name to pull from wmbusmeters/wmbusmeters")
    args = parser.parse_args()

    remote_url = "https://github.com/wmbusmeters/wmbusmeters"
    prefix = "components/wmbus_common"

    # Fetch the tag directly without adding a remote
    run(f"git fetch {remote_url} tag {args.tag}")

    # Pull subtree, squash into one commit
    run(f"git subtree pull --prefix={prefix} {remote_url} {args.tag} --squash")

    # Create or overwrite a hidden file in the target directory with the tag name
    tag_file = parent_dir / prefix / ".wmbusmeters_tag"
    tag_file.write_text(args.tag)


if __name__ == "__main__":
    main()
