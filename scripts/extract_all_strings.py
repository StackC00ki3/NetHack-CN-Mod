#!/usr/bin/env python3
import argparse
import csv
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Tuple

SOURCE_EXTS = {
    ".c",
    ".h",
    ".cpp",
    ".cc",
    ".cxx",
    ".hpp",
    ".hh",
    ".hxx",
    ".rc",
}

PRINTABLE_ASCII = set(range(32, 127))


@dataclass
class StringHit:
    source: str
    file: str
    line: int
    text: str


def decode_c_string_literal(content: str) -> str:
    # Decode common C escapes but keep unknown escapes as-is.
    def repl(match: re.Match) -> str:
        seq = match.group(0)
        if seq == r"\\n":
            return "\n"
        if seq == r"\\r":
            return "\r"
        if seq == r"\\t":
            return "\t"
        if seq == r"\\\\":
            return "\\"
        if seq == r'\\"':
            return '"'
        if seq == r"\\'":
            return "'"
        if seq.startswith(r"\\x"):
            try:
                return chr(int(seq[2:], 16))
            except ValueError:
                return seq
        if re.match(r"\\[0-7]{1,3}$", seq):
            try:
                return chr(int(seq[1:], 8))
            except ValueError:
                return seq
        return seq

    return re.sub(r"\\x[0-9A-Fa-f]{1,2}|\\[0-7]{1,3}|\\.|$", repl, content)


def iter_c_like_string_literals(text: str) -> Iterable[Tuple[int, str]]:
    i = 0
    n = len(text)
    line = 1

    in_line_comment = False
    in_block_comment = False

    while i < n:
        ch = text[i]

        if ch == "\n":
            line += 1
            in_line_comment = False
            i += 1
            continue

        if in_line_comment:
            i += 1
            continue

        if in_block_comment:
            if ch == "*" and i + 1 < n and text[i + 1] == "/":
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue

        # comment start
        if ch == "/" and i + 1 < n:
            nxt = text[i + 1]
            if nxt == "/":
                in_line_comment = True
                i += 2
                continue
            if nxt == "*":
                in_block_comment = True
                i += 2
                continue

        # string prefixes: L"..." u"..." U"..." u8"..."
        if ch in ("L", "u", "U"):
            if i + 1 < n and text[i + 1] == '"':
                i += 1
                ch = text[i]
            elif ch == "u" and i + 2 < n and text[i + 1] == "8" and text[i + 2] == '"':
                i += 2
                ch = text[i]

        # char literal: skip
        if ch == "'":
            i += 1
            while i < n:
                c = text[i]
                if c == "\\" and i + 1 < n:
                    i += 2
                    continue
                if c == "'":
                    i += 1
                    break
                if c == "\n":
                    line += 1
                i += 1
            continue

        # string literal
        if ch == '"':
            start_line = line
            i += 1
            buf = []
            while i < n:
                c = text[i]
                if c == "\\" and i + 1 < n:
                    buf.append(text[i : i + 2])
                    i += 2
                    continue
                if c == '"':
                    i += 1
                    break
                if c == "\n":
                    line += 1
                buf.append(c)
                i += 1

            raw = "".join(buf)
            yield start_line, decode_c_string_literal(raw)
            continue

        i += 1


def extract_from_source_file(path: Path, root: Path, min_len: int) -> List[StringHit]:
    hits: List[StringHit] = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return hits

    rel = str(path.relative_to(root)).replace("\\", "/")

    for line, s in iter_c_like_string_literals(text):
        s = s.strip()
        if len(s) < min_len:
            continue
        hits.append(StringHit(source="source", file=rel, line=line, text=s))

    return hits


def is_printable_ascii_byte(b: int) -> bool:
    return b in PRINTABLE_ASCII


def extract_ascii_strings(data: bytes, min_len: int) -> List[Tuple[int, str]]:
    out: List[Tuple[int, str]] = []
    i = 0
    n = len(data)
    while i < n:
        if not is_printable_ascii_byte(data[i]):
            i += 1
            continue
        start = i
        while i < n and is_printable_ascii_byte(data[i]):
            i += 1
        if i - start >= min_len:
            out.append((start, data[start:i].decode("ascii", errors="ignore")))
    return out


def extract_utf16le_strings(data: bytes, min_len: int) -> List[Tuple[int, str]]:
    out: List[Tuple[int, str]] = []
    i = 0
    n = len(data)
    while i + 1 < n:
        if not (is_printable_ascii_byte(data[i]) and data[i + 1] == 0):
            i += 1
            continue
        start = i
        chars = []
        while i + 1 < n and is_printable_ascii_byte(data[i]) and data[i + 1] == 0:
            chars.append(chr(data[i]))
            i += 2
        if len(chars) >= min_len:
            out.append((start, "".join(chars)))
    return out


def extract_from_binary(path: Path, root: Path, min_len: int) -> List[StringHit]:
    hits: List[StringHit] = []
    try:
        data = path.read_bytes()
    except OSError:
        return hits

    rel = str(path.relative_to(root)).replace("\\", "/")

    for off, s in extract_ascii_strings(data, min_len):
        hits.append(StringHit(source="binary-ascii", file=rel, line=off, text=s))

    for off, s in extract_utf16le_strings(data, min_len):
        hits.append(StringHit(source="binary-utf16le", file=rel, line=off, text=s))

    return hits


def collect_source_files(root: Path) -> Iterable[Path]:
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() not in SOURCE_EXTS:
            continue
        # Skip generated/build trees
        parts = {part.lower() for part in p.parts}
        if "build" in parts or "cmake-build-debug" in parts or ".git" in parts:
            continue
        yield p


def dedupe_hits(hits: List[StringHit]) -> List[StringHit]:
    seen = set()
    out: List[StringHit] = []
    for h in hits:
        key = (h.source, h.text)
        if key in seen:
            continue
        seen.add(key)
        out.append(h)
    return out


def write_csv(path: Path, hits: List[StringHit]) -> None:
    with path.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["source", "file", "line_or_offset", "text"])
        for h in hits:
            w.writerow([h.source, h.file, h.line, h.text])


def write_json(path: Path, hits: List[StringHit]) -> None:
    data = [
        {
            "source": h.source,
            "file": h.file,
            "line_or_offset": h.line,
            "text": h.text,
        }
        for h in hits
    ]
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract hardcoded strings from NetHack source and binaries."
    )
    parser.add_argument(
        "--root",
        default=".",
        help="Project root directory (default: current directory)",
    )
    parser.add_argument(
        "--min-len",
        type=int,
        default=4,
        help="Minimum string length to keep (default: 4)",
    )
    parser.add_argument(
        "--binary",
        action="append",
        default=[],
        help="Relative path of a binary to scan (can be used multiple times)",
    )
    parser.add_argument(
        "--out-prefix",
        default="output/dump_hardcoded_strings",
        help="Output file prefix (default: output/dump_hardcoded_strings)",
    )
    parser.add_argument(
        "--unique-only",
        action="store_true",
        help="Keep only unique texts per source type",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()

    all_hits: List[StringHit] = []

    for src_file in collect_source_files(root):
        all_hits.extend(extract_from_source_file(src_file, root, args.min_len))

    for b in args.binary:
        bp = (root / b).resolve()
        if not bp.exists() or not bp.is_file():
            print(f"[WARN] Binary not found: {bp}")
            continue
        all_hits.extend(extract_from_binary(bp, root, args.min_len))

    if args.unique_only:
        all_hits = dedupe_hits(all_hits)

    all_hits.sort(key=lambda x: (x.source, x.file, x.line, x.text))

    out_csv = root / f"{args.out_prefix}.csv"
    out_json = root / f"{args.out_prefix}.json"
    out_csv.parent.mkdir(parents=True, exist_ok=True)

    write_csv(out_csv, all_hits)
    write_json(out_json, all_hits)

    print(f"Done. total={len(all_hits)}")
    print(f"CSV : {out_csv}")
    print(f"JSON: {out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
