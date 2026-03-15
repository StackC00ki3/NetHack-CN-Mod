#!/usr/bin/env python3
"""
Extract changed C string literals from two source trees (English vs Chinese)
and export as JSON mapping grouped by file:
{
    "allmain.c": {
        "English": "Chinese"
    }
}

Usage:
  python extract_c_string_diff.py \
      --en-dir "D:/path/to/english/src" \
      --zh-dir "D:/path/to/chinese/src" \
      --output strings_map.json
"""

from __future__ import annotations

import argparse
import difflib
import json
import re
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

# C string literal matcher (supports optional u8/u/U/L prefixes).
C_STRING_RE = re.compile(r'(?:u8|u|U|L)?"(?:\\.|[^"\\])*"')

# printf-style format specifier detector.
HAS_FORMAT_SPEC_RE = re.compile(
    r"%(?!%)[-+0 #]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh?|ll?|[jztL])?[diouxXeEfFgGaAcspn]"
)


def read_text_auto(path: Path) -> str:
    """Read text with common fallback encodings."""
    for enc in ("utf-8", "utf-8-sig", "gb18030", "cp936", "latin1"):
        try:
            return path.read_text(encoding=enc)
        except UnicodeDecodeError:
            continue
    # Last resort
    return path.read_text(encoding="utf-8", errors="replace")


def unquote_c_literal(token: str) -> str:
    """Convert a C string token like u8\"a\\nb\" into plain text."""
    # Strip prefix if present.
    if token.startswith("u8\""):
        core = token[3:-1]
    elif token.startswith(("u\"", "U\"", "L\"")):
        core = token[2:-1]
    else:
        core = token[1:-1]

    # Minimal C-style unescape to avoid mojibake on non-ASCII text.
    # We intentionally handle common escapes used in source strings.
    out: List[str] = []
    i = 0
    n = len(core)
    while i < n:
        ch = core[i]
        if ch != "\\" or i + 1 >= n:
            out.append(ch)
            i += 1
            continue

        nxt = core[i + 1]
        if nxt == "n":
            out.append("\n")
        elif nxt == "t":
            out.append("\t")
        elif nxt == "r":
            out.append("\r")
        elif nxt == "b":
            out.append("\b")
        elif nxt == "f":
            out.append("\f")
        elif nxt == "v":
            out.append("\v")
        elif nxt == "a":
            out.append("\a")
        elif nxt in ('\\', '"', "'", "?"):
            out.append(nxt)
        else:
            # Keep unknown escapes as-is (drop escape slash only).
            out.append(nxt)
        i += 2

    return "".join(out)


def extract_strings_from_lines(lines: Sequence[str]) -> List[str]:
    out: List[str] = []
    for line in lines:
        for m in C_STRING_RE.finditer(line):
            out.append(unquote_c_literal(m.group(0)))
    return out


def pair_changed_strings(
    en_lines: Sequence[str], zh_lines: Sequence[str]
) -> List[Tuple[str, str]]:
    """
    Compare changed line blocks and pair string literals by position
    inside replace hunks.
    """
    pairs: List[Tuple[str, str]] = []
    sm = difflib.SequenceMatcher(a=en_lines, b=zh_lines, autojunk=False)

    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag != "replace":
            continue
        en_block = en_lines[i1:i2]
        zh_block = zh_lines[j1:j2]

        en_strs = extract_strings_from_lines(en_block)
        zh_strs = extract_strings_from_lines(zh_block)

        # Pair by order within this changed block.
        n = min(len(en_strs), len(zh_strs))
        for k in range(n):
            e = en_strs[k].strip()
            z = zh_strs[k].strip()
            if not e or not z:
                continue
            if e == z:
                continue
            pairs.append((e, z))

    return pairs


def _is_func_call_arg(source: str, str_start: int) -> bool:
    """Heuristic: return True if the string literal at *str_start* sits
    inside a function-call argument list (i.e. ``func(... "str" ...)``).

    Scans backward from *str_start*, tracking parenthesis depth.
    If we reach a ``(`` at depth 0 preceded by an identifier it is a call.
    If we hit ``=``, ``{``, ``}``, or ``;`` at depth 0 first it is not.
    """
    pos = str_start - 1
    depth = 0
    while pos >= 0:
        ch = source[pos]
        if ch in " \t\n\r":
            pos -= 1
        elif ch == ")":
            depth += 1
            pos -= 1
        elif ch == "(":
            if depth == 0:
                p = pos - 1
                while p >= 0 and source[p] in " \t\n\r":
                    p -= 1
                return p >= 0 and (source[p].isalnum() or source[p] == "_")
            depth -= 1
            pos -= 1
        elif ch in ";{}=" and depth == 0:
            return False
        elif ch == '"':
            # Back-skip over a preceding string literal.
            pos -= 1
            while pos >= 0:
                if source[pos] == '"':
                    bs = 0
                    p = pos - 1
                    while p >= 0 and source[p] == "\\":
                        bs += 1
                        p -= 1
                    if bs % 2 == 0:
                        break
                pos -= 1
            # Skip optional prefix (u8 / u / U / L).
            if pos > 0 and source[pos - 1] == "8" and pos > 1 and source[pos - 2] == "u":
                pos -= 3
            elif pos > 0 and source[pos - 1] in "uUL":
                pos -= 2
            else:
                pos -= 1
        else:
            pos -= 1
    return False


def find_string_args_in_call(source: str, fmt_str: str) -> List[str]:
    """Find string literal arguments in the same function call as *fmt_str*.

    Locates every occurrence of the C string literal whose unquoted value
    equals *fmt_str*, then scans forward inside the enclosing call to
    collect subsequent string-literal arguments (at the same paren depth).
    Returns an empty list when the format string is not a function-call
    argument (e.g. variable initializer, array literal).
    """
    results: List[str] = []

    for m in C_STRING_RE.finditer(source):
        if unquote_c_literal(m.group(0)) != fmt_str:
            continue

        if not _is_func_call_arg(source, m.start()):
            continue

        pos = m.end()
        depth = 0
        done = False

        while pos < len(source) and not done:
            ch = source[pos]

            if ch == "(":
                depth += 1
                pos += 1
            elif ch == ")":
                if depth <= 0:
                    done = True
                else:
                    depth -= 1
                    pos += 1
            elif ch == "'":
                # Skip character literal  'x' or '\n' etc.
                pos += 1
                if pos < len(source) and source[pos] == "\\":
                    pos += 1
                pos += 1  # actual char
                if pos < len(source) and source[pos] == "'":
                    pos += 1
            elif ch == "/" and pos + 1 < len(source) and source[pos + 1] == "/":
                nl = source.find("\n", pos)
                pos = nl + 1 if nl >= 0 else len(source)
            elif ch == "/" and pos + 1 < len(source) and source[pos + 1] == "*":
                end_cm = source.find("*/", pos + 2)
                pos = end_cm + 2 if end_cm >= 0 else len(source)
            else:
                sm = C_STRING_RE.match(source, pos)
                if sm:
                    arg = unquote_c_literal(sm.group(0)).strip()
                    if arg:
                        results.append(arg)
                    pos = sm.end()
                else:
                    pos += 1

    # Deduplicate while preserving order.
    seen: set = set()
    unique: List[str] = []
    for a in results:
        if a not in seen:
            seen.add(a)
            unique.append(a)
    return unique


def iter_c_files(root: Path) -> Iterable[Path]:
    yield from root.rglob("*.c")


def build_mapping(en_dir: Path, zh_dir: Path) -> Dict[str, Dict[str, str]]:
    mapping: Dict[str, Dict[str, str]] = {}

    en_files = {p.relative_to(en_dir).as_posix(): p for p in iter_c_files(en_dir)}
    zh_files = {p.relative_to(zh_dir).as_posix(): p for p in iter_c_files(zh_dir)}

    common = sorted(set(en_files) & set(zh_files))

    for rel in common:
        en_path = en_files[rel]
        zh_path = zh_files[rel]

        en_text = read_text_auto(en_path)
        zh_text = read_text_auto(zh_path)

        if en_text == zh_text:
            continue

        en_lines = en_text.splitlines()
        zh_lines = zh_text.splitlines()

        file_map = mapping.setdefault(rel, {})
        for e, z in pair_changed_strings(en_lines, zh_lines):
            if e in file_map:
                continue
            # Format strings get the fmt/arg structure.
            if HAS_FORMAT_SPEC_RE.search(e):
                file_map[e] = {"fmt": z, "arg": {}}
            else:
                file_map[e] = z

        # Smart arg: for each format-string entry, find its string args
        # in the English source and move matching translations into arg.
        fmt_entries = {
            e: v
            for e, v in file_map.items()
            if isinstance(v, dict) and "fmt" in v
        }
        if fmt_entries:
            for fmt_en, entry in fmt_entries.items():
                call_args = find_string_args_in_call(en_text, fmt_en)
                for arg_en in call_args:
                    if arg_en in file_map and isinstance(file_map[arg_en], str):
                        entry["arg"][arg_en] = file_map[arg_en]
            # Remove top-level entries that were moved into an arg dict.
            all_moved: set = set()
            for entry in fmt_entries.values():
                all_moved.update(entry["arg"].keys())
            for key in all_moved:
                file_map.pop(key, None)

        # If no strings were found for this changed file, remove empty dict.
        if not file_map:
            mapping.pop(rel, None)

    return mapping


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract changed C string literals from two dirs into JSON"
    )
    parser.add_argument("--en-dir", required=True, help="English source root dir")
    parser.add_argument("--zh-dir", required=True, help="Chinese source root dir")
    parser.add_argument(
        "--output",
        default="c_string_diff_map.json",
        help="Output JSON file path (default: c_string_diff_map.json)",
    )
    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Pretty-print JSON output",
    )

    args = parser.parse_args()

    en_dir = Path(args.en_dir).resolve()
    zh_dir = Path(args.zh_dir).resolve()
    out_path = Path(args.output).resolve()

    if not en_dir.is_dir():
        raise SystemExit(f"--en-dir is not a directory: {en_dir}")
    if not zh_dir.is_dir():
        raise SystemExit(f"--zh-dir is not a directory: {zh_dir}")

    mapping = build_mapping(en_dir, zh_dir)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        if args.pretty:
            json.dump(mapping, f, ensure_ascii=False, indent=2)
        else:
            json.dump(mapping, f, ensure_ascii=False)

    file_count = len(mapping)
    pair_count = sum(len(v) for v in mapping.values())
    print(
        f"Done. Extracted {pair_count} string mappings across "
        f"{file_count} files -> {out_path}"
    )


if __name__ == "__main__":
    main()
