#!/usr/bin/env python3
# coding: utf-8
"""
verify_dict.py -- end-to-end verification of a .prism.bin + .table.bin
pair. Walks the index tree exactly the way our C++ Table::Query would,
and prints the candidate words for a given syllable sequence.

This is the closest thing to an on-device integration test we can run
on the dev host (RimeCore.dll itself is cross-compiled for ARMV4I and
can't be loaded here).

Usage:
  python verify_dict.py <basename> <syllable1> [syllable2 ...]

Example:
  python verify_dict.py data/luna_pinyin ni
  python verify_dict.py data/luna_pinyin ni hao
"""

import io
import struct
import sys

# Encourage UTF-8 stdout even when running under a cp936 console.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
else:
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8",
                                  errors="replace")


SIZE_HEAD_INDEX_NODE = 12
SIZE_TRUNK_INDEX_NODE = 16
SIZE_LONG_ENTRY = 16
SIZE_ENTRY = 8
INDEX_CODE_MAX_LENGTH = 3


def read_u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def read_i32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def read_f32(buf, off):
    return struct.unpack_from("<f", buf, off)[0]


def offsetptr(buf, field_off):
    """Read an OffsetPtr field; return absolute target offset, or 0 (NULL)."""
    rel = read_i32(buf, field_off)
    return (field_off + rel) if rel else 0


def read_string_table(buf, off, size):
    end = off + size
    assert buf[off:off + 4] == b"RST1", "string table magic mismatch"
    count = read_u32(buf, off + 4)
    pos = off + 8
    out = []
    for _ in range(count):
        ln = read_u32(buf, pos)
        pos += 4
        out.append(buf[pos:pos + ln].decode("utf-8", errors="replace"))
        pos += ln
    return out


def parse_table(table_path):
    with open(table_path, "rb") as f:
        buf = f.read()
    assert buf[:12] == b"Rime::Table/", "not a table.bin"
    n_syll = read_u32(buf, 36)
    syllabary_off = offsetptr(buf, 44)
    index_off = offsetptr(buf, 48)
    st_off = offsetptr(buf, 60)
    st_size = read_u32(buf, 64)

    strings = read_string_table(buf, st_off, st_size)

    # syllabary[i] = str_id pointing into `strings`
    syll_strings = []
    for i in range(n_syll):
        str_id = read_i32(buf, syllabary_off + 4 + i * 4)
        syll_strings.append(strings[str_id])

    # syllable -> id  (must match upstream's sorted-set order)
    syll_to_id = {s: i for i, s in enumerate(syll_strings)}

    return {
        "buf": buf,
        "strings": strings,
        "syllables": syll_strings,
        "syll_to_id": syll_to_id,
        "index_off": index_off,
    }


def read_entry_list(buf, list_off, count, strings):
    """list_off = absolute offset of the Entry[] array."""
    out = []
    for i in range(count):
        e_off = list_off + i * SIZE_ENTRY
        str_id = read_i32(buf, e_off + 0)
        weight = read_f32(buf, e_off + 4)
        text = strings[str_id] if 0 <= str_id < len(strings) else "?"
        out.append((text, weight))
    return out


def walk_head(t, syll_id):
    """Return (entries_list, next_level_offset) for syll_id at head level."""
    buf = t["buf"]
    base = t["index_off"] + 4
    n_off = base + syll_id * SIZE_HEAD_INDEX_NODE
    ecount = read_u32(buf, n_off + 0)
    e_at_off = offsetptr(buf, n_off + 4)
    next_off = offsetptr(buf, n_off + 8)
    entries = read_entry_list(buf, e_at_off, ecount, t["strings"]) \
              if e_at_off else []
    return entries, next_off


def walk_trunk(t, trunk_off, syll_id):
    """Binary-search a TrunkIndex (sorted by key) for syll_id. Return
    (entries_list, next_level_off) or None."""
    buf = t["buf"]
    n = read_u32(buf, trunk_off)
    base = trunk_off + 4
    lo, hi = 0, n
    while lo < hi:
        mid = (lo + hi) // 2
        n_off = base + mid * SIZE_TRUNK_INDEX_NODE
        key = read_i32(buf, n_off + 0)
        if key == syll_id:
            ecount = read_u32(buf, n_off + 4)
            e_at = offsetptr(buf, n_off + 8)
            nxt = offsetptr(buf, n_off + 12)
            entries = read_entry_list(buf, e_at, ecount, t["strings"]) \
                      if e_at else []
            return entries, nxt
        if key < syll_id:
            lo = mid + 1
        else:
            hi = mid
    return None


def walk_tail(t, tail_off, code_ids, depth):
    """Tail index contains LongEntry[] with extra_code beyond
    kIndexCodeMaxLength. Match entries whose extra_code matches the
    remainder of code_ids starting at `depth`."""
    buf = t["buf"]
    n = read_u32(buf, tail_off)
    base = tail_off + 4
    matches = []
    want_extra = list(code_ids[depth:])
    for i in range(n):
        le_off = base + i * SIZE_LONG_ENTRY
        ec_size = read_u32(buf, le_off + 0)
        ec_at = offsetptr(buf, le_off + 4)
        # Read extra_code SyllableIds
        extra = [read_i32(buf, ec_at + j * 4) for j in range(ec_size)] \
                if ec_at else []
        if extra == want_extra:
            text_id = read_i32(buf, le_off + 8)
            weight = read_f32(buf, le_off + 12)
            matches.append(
                (t["strings"][text_id] if 0 <= text_id < len(t["strings"])
                 else "?",
                 weight))
    return matches


def lookup(t, code_ids):
    """Return list of (text, weight) for the given code (tuple of
    syllable ids). Walks the index exactly like Table::QueryPhrases."""
    n = len(code_ids)
    if n == 0:
        return []
    # Walk down INDEX_CODE_MAX_LENGTH levels at most, then tail.
    entries, next_off = walk_head(t, code_ids[0])
    if n == 1:
        return entries
    depth = 1
    while depth < n:
        if next_off == 0:
            return []
        if depth < INDEX_CODE_MAX_LENGTH:
            # next_off points to TrunkIndex
            result = walk_trunk(t, next_off, code_ids[depth])
            if not result:
                return []
            entries, next_off = result
            depth += 1
            if depth == n:
                return entries
        else:
            # depth == INDEX_CODE_MAX_LENGTH: next_off is a TailIndex
            return walk_tail(t, next_off, code_ids, depth)
    return entries


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        sys.exit(2)
    base = argv[1]
    syllables = argv[2:]
    t = parse_table(base + ".table.bin")

    # Map syllables to ids.
    code_ids = []
    for s in syllables:
        sid = t["syll_to_id"].get(s)
        if sid is None:
            print("syllable %r not in syllabary" % s)
            sys.exit(1)
        code_ids.append(sid)

    print("code: %s -> ids %s" % (" ".join(syllables), code_ids))
    results = lookup(t, tuple(code_ids))
    print("found %d candidates" % len(results))
    # Sort by weight desc for display
    results.sort(key=lambda e: -e[1])
    for text, weight in results[:20]:
        print("  %-12s w=%g" % (text, weight))


if __name__ == "__main__":
    main(sys.argv)
