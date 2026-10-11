"""Command-line entry point for analyzing a saved UE Blueprint snapshot."""
import argparse
import json
from pathlib import Path
from .schemas import SnapshotError, parse_snapshot
from .ir_builder import build_ir


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Blueprint Snapshot JSON")
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    try:
        raw = json.loads(args.snapshot.read_text(encoding="utf-8-sig"))
        result = build_ir(parse_snapshot(raw))
    except (OSError, ValueError, SnapshotError) as exc:
        parser.exit(2, f"Analysis failed: {exc}\n")
    payload = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(payload + "\n", encoding="utf-8")
    else:
        print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
