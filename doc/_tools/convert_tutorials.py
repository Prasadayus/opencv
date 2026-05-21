"""Batch-convert all .markdown tutorial files in doc/tutorials/ to .md (MyST).

Skips files where the .md is already newer than the .markdown source.
Output .md files are placed next to the source .markdown files and are
gitignored — they are build artifacts generated at build time.

Usage:
  python3 convert_tutorials.py <tutorials_dir>
                               [--tag /path/to/opencv.tag]
                               [--local /path/to/local_refs.json]
                               [--force]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dox2myst


def convert_all(tutorials_dir: Path, tags: dox2myst.TagIndex,
                local_refs: dict, force: bool) -> None:
    markdowns = sorted(tutorials_dir.rglob("*.markdown"))
    if not markdowns:
        print(f"No .markdown files found under {tutorials_dir}")
        return

    converted = skipped = 0
    for src in markdowns:
        dst = src.with_suffix(".md")
        if not force and dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime:
            skipped += 1
            continue

        text = src.read_text(encoding="utf-8")
        out_doc = str(dst.relative_to(tutorials_dir.parent)).replace("\\", "/").removesuffix(".md")
        result = dox2myst.transform(text, tags, local_refs=local_refs,
                                    out_doc=out_doc, image_prefix="images")
        dst.write_text(result, encoding="utf-8")
        print(f"  converted: {src.relative_to(tutorials_dir.parent)}")
        converted += 1

    print(f"convert_tutorials: {converted} converted, {skipped} up-to-date")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("tutorials_dir", type=Path)
    ap.add_argument("--tag", type=Path, default=Path("/tmp/opencv.tag"))
    ap.add_argument("--local", type=Path, default=None)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    if not args.tag.exists():
        print(f"WARNING: tagfile not found at {args.tag} — @ref links will use external URLs",
              file=sys.stderr)
        tags = dox2myst.TagIndex.__new__(dox2myst.TagIndex)
        tags.by_name = {}
        tags.pages = {}
    else:
        tags = dox2myst.TagIndex(args.tag)

    local_refs: dict = {}
    if args.local and args.local.exists():
        local_refs = json.loads(args.local.read_text(encoding="utf-8"))

    convert_all(args.tutorials_dir, tags, local_refs, args.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
