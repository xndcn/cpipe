#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 cpipe contributors

"""Migrate a pipeline.cpipe.json file from schema v0.3 to v0.4 in place."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


V03_SCHEMA = "https://schemas.cpipe.dev/pipeline/v0.3.json"
V04_SCHEMA = "https://schemas.cpipe.dev/pipeline/v0.4.json"


def migrate(path: Path) -> None:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("version") != "0.3":
        raise SystemExit(f"{path}: expected pipeline version 0.3")
    document["version"] = "0.4"
    if document.get("$schema") == V03_SCHEMA:
        document["$schema"] = V04_SCHEMA
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pipeline", type=Path, help="pipeline.cpipe.json to migrate in place")
    args = parser.parse_args()
    migrate(args.pipeline)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
