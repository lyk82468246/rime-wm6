#!/usr/bin/env python3
# coding: utf-8
"""
inspect_bin.py -- read a .prism.bin or .table.bin produced by
build_dict.py and dump its header + a sample of contents. Use this
to sanity-check the writer before deploying to the device.

Usage:
  python inspect_bin.py <luna_pinyin.prism.bin>
  python inspect_bin.py <luna_pinyin.table.bin>
"""

import struct
import sys


def read_cstring(buf, off, max_len):
    end = buf.find(b"\0", off, off + max_len)
    if end < 0:
        end = off + max_len
    return buf[off:end].decode("utf-8", errors="replace")


def read_u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def read_i32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def read_string_table(buf, off, size):
    end = off + size
    if buf[off:off + 4] != b"RST1":
        raise ValueError("string table magic mismatch: %r" % buf[off:off+4])
    count = read_u32(buf, off + 4)
    pos = off + 8
    out = []
    for _ in range(count):
        if pos + 4 > end:
            raise ValueError("truncated string table")
        ln = read_u32(buf, pos)
        pos += 4
        out.append(buf[pos:pos + ln].decode("utf-8", errors="replace"))
        pos += ln
    return out


def inspect_prism(buf):
    fmt = read_cstring(buf, 0, 32)
    print("format:           %r" % fmt)
    if not fmt.startswith("Rime::PrismFlat/"):
        print("(not a flat-mode prism; aborting)")
        return
    cksum = read_u32(buf, 32)
    n = read_u32(buf, 36)
    st_off = read_u32(buf, 40)
    st_size = read_u32(buf, 44)
    print("dict_checksum:    0x%08x" % cksum)
    print("num_syllables:    %d" % n)
    print("string_table_off: %d" % st_off)
    print("string_table_sz:  %d" % st_size)
    syllables = read_string_table(buf, st_off, st_size)
    print("first 10 syll:    %s" % syllables[:10])
    print("last  5 syll:    %s" % syllables[-5:])
    if len(syllables) != n:
        print("WARN: count mismatch (header=%d vs stringtable=%d)"
              % (n, len(syllables)))
    # Spot-check sorted invariant.
    for i in range(1, min(50, len(syllables))):
        if syllables[i - 1] >= syllables[i]:
            print("WARN: sort violation at i=%d: %r >= %r"
                  % (i, syllables[i - 1], syllables[i]))


def inspect_table(buf):
    fmt = read_cstring(buf, 0, 32)
    print("format:           %r" % fmt)
    if not fmt.startswith("Rime::Table/"):
        print("(not our table format; aborting)")
        return
    cksum = read_u32(buf, 32)
    n_syll = read_u32(buf, 36)
    n_entries = read_u32(buf, 40)
    syll_offptr = read_i32(buf, 44)  # relative
    idx_offptr = read_i32(buf, 48)   # relative
    st_offptr = read_i32(buf, 60)    # relative
    st_size = read_u32(buf, 64)
    syllabary_off = 44 + syll_offptr
    index_off = 48 + idx_offptr
    string_table_off = 60 + st_offptr
    print("dict_checksum:    0x%08x" % cksum)
    print("num_syllables:    %d" % n_syll)
    print("num_entries:      %d" % n_entries)
    print("syllabary off:    %d" % syllabary_off)
    print("index off:        %d" % index_off)
    print("string_table off: %d (size %d)" % (string_table_off, st_size))

    # Syllabary: Array<StringType> = uint32 size + (int32 str_id)*size
    syll_count = read_u32(buf, syllabary_off)
    print("syllabary.size:   %d" % syll_count)
    syll_ids = [read_i32(buf, syllabary_off + 4 + i * 4)
                for i in range(min(syll_count, 5))]
    print("first 5 syll_ids: %s" % syll_ids)

    # String table
    strings = read_string_table(buf, string_table_off, st_size)
    print("string_table count: %d" % len(strings))
    print("strings[0:5]:     %s" % strings[:5])
    print("strings[-5:]:     %s" % strings[-5:])

    # HeadIndex: uint32 size + HeadIndexNode[size] (12 bytes each)
    head_size = read_u32(buf, index_off)
    print("head_index.size:  %d" % head_size)
    if head_size != n_syll:
        print("WARN: head_index size != num_syllables")

    # Inspect first 5 head index nodes and their entries.
    base = index_off + 4
    for sid in range(min(head_size, 5)):
        n_off = base + sid * 12
        e_count = read_u32(buf, n_off + 0)
        e_at_relptr = read_i32(buf, n_off + 4)
        next_relptr = read_i32(buf, n_off + 8)
        if e_count == 0 and e_at_relptr == 0 and next_relptr == 0:
            continue
        e_at_off = (n_off + 4) + e_at_relptr if e_at_relptr else 0
        # Get the syllable string for this id
        if sid < syll_count:
            syl_str_id = read_i32(buf, syllabary_off + 4 + sid * 4)
            syl_text = strings[syl_str_id] if 0 <= syl_str_id < len(strings) \
                       else "?"
        else:
            syl_text = "?"
        print("  head[%d] (syll=%r): %d entries @ %d, next_level @ %s"
              % (sid, syl_text, e_count, e_at_off,
                 ((n_off + 8) + next_relptr) if next_relptr else "(none)"))
        # Show first entry's text+weight.
        if e_count > 0 and e_at_off > 0:
            text_str_id = read_i32(buf, e_at_off + 0)
            weight = struct.unpack_from("<f", buf, e_at_off + 4)[0]
            text = strings[text_str_id] if 0 <= text_str_id < len(strings) \
                   else "?"
            print("    first entry: text=%r weight=%g" % (text, weight))


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        sys.exit(2)
    path = argv[1]
    with open(path, "rb") as f:
        buf = f.read()
    print("file:             %s (%d bytes)" % (path, len(buf)))
    if path.endswith(".prism.bin"):
        inspect_prism(buf)
    elif path.endswith(".table.bin"):
        inspect_table(buf)
    else:
        # Sniff by header
        head = buf[:16]
        if head.startswith(b"Rime::PrismFlat/"):
            inspect_prism(buf)
        elif head.startswith(b"Rime::Table/"):
            inspect_table(buf)
        else:
            print("unknown format: %r" % head)


if __name__ == "__main__":
    main(sys.argv)
