#!/usr/bin/env python3
"""Convert two-column stdin text into an {"en": "cn"} JSON object.

Input format (one pair per line):
  adornment 装饰品
  hunger 饥饿

The script accepts common separators:
  - tab(s)
  - vertical bar (|)
  - first CJK character boundary (useful for OCR/plain text)

Usage:
  python scripts/stdin_pairs_to_json.py < pairs.txt
  cat pairs.txt | python scripts/stdin_pairs_to_json.py --sort-keys
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from typing import Dict, Iterable, Tuple


CJK_RE = re.compile(r"[\u3400-\u9fff\uf900-\ufaff]")
ASCII_KEY_RE = re.compile(r"^[A-Za-z0-9_.+\-/]+$")


def split_pair_line(line: str) -> Tuple[str, str]:
    """Split one text line into (en_key, cn_value)."""
    text = line.strip().strip("|")
    if not text:
        raise ValueError("empty line")

    # Preferred split: table-like separators.
    if "|" in text:
        parts = [p.strip() for p in text.split("|") if p.strip()]
        if len(parts) >= 2:
            return parts[0], " ".join(parts[1:]).strip()

    tab_parts = [p.strip() for p in re.split(r"\t+", text) if p.strip()]
    if len(tab_parts) >= 2:
        return tab_parts[0], " ".join(tab_parts[1:]).strip()

    # Fallback: split at the first CJK character.
    m = CJK_RE.search(text)
    if m is not None:
        left = text[: m.start()].strip()
        right = text[m.start() :].strip()
        if left and right:
            return left, right

    # Last fallback: split once by whitespace.
    parts = text.split(maxsplit=1)
    if len(parts) == 2:
        return parts[0].strip(), parts[1].strip()

    raise ValueError("cannot split into key/value")


def iter_input_lines() -> Iterable[str]:
    """Yield input lines from stdin.

    In interactive terminal mode, an empty line ends input so users can
    finish by pressing Enter on a blank line.
    """
    if sys.stdin.isatty():
        while True:
            try:
                raw = input()
            except EOFError:
                break
            if raw == "":
                break
            yield raw
        return

    for raw in sys.stdin.read().splitlines():
        yield raw


def parse_stdin(strict: bool) -> Dict[str, str]:
    """Parse stdin lines into mapping."""
    out: Dict[str, str] = {}
    for idx, raw in enumerate(iter_input_lines(), start=1):
        line = raw.strip()
        if not line:
            continue

        try:
            en, cn = split_pair_line(line)
        except ValueError as exc:
            msg = f"Line {idx}: {exc}; content={raw!r}"
            if strict:
                raise ValueError(msg) from exc
            print(msg, file=sys.stderr)
            continue

        if not ASCII_KEY_RE.match(en):
            msg = f"Line {idx}: key looks unusual: {en!r}"
            if strict:
                raise ValueError(msg)
            print(msg, file=sys.stderr)

        if en in out and out[en] != cn:
            msg = f"Line {idx}: duplicate key with different value: {en!r}"
            if strict:
                raise ValueError(msg)
            print(msg, file=sys.stderr)

        out[en] = cn

    return out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convert stdin two-column text to en:cn JSON")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Treat malformed lines or duplicate conflicts as errors",
    )
    parser.add_argument(
        "--sort-keys",
        action="store_true",
        help="Sort JSON keys before output",
    )
    return parser


def main() -> None:
    args = build_parser().parse_args()
    data = parse_stdin(strict=args.strict)
    json.dump(data, sys.stdout, ensure_ascii=False, indent=2, sort_keys=args.sort_keys)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()