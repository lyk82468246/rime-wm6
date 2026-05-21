#!/usr/bin/env python3
# coding: utf-8
"""
build_dict.py -- desktop tool that converts an upstream Rime
luna_pinyin.dict.yaml into the WinCE-port's custom .prism.bin and
.table.bin format.

Output files:
  * <out>.prism.bin -- format "Rime::PrismFlat/1.0". On-device,
    Prism::Load reads the embedded sorted syllable list and BUILDS the
    Darts double-array trie in-memory. Lookup speed is identical to
    the upstream darts-on-disk format, at the cost of a one-time
    build at Load time. Not compatible with upstream `.prism.bin`.

  * <out>.table.bin -- format "Rime::Table/4.0". Layout (Metadata
    header, Syllabary, HeadIndex/TrunkIndex/TailIndex tree, embedded
    string table) is byte-compatible with our ported table.cc, BUT
    the embedded string table is "RST1" format (our stub
    StringTable), NOT marisa-format. Not compatible with upstream
    `.table.bin` until the marisa replacement lands.

Algebra rules are NOT applied at build time. The on-disk prism only
contains syllables that literally appear in the dict file. Once
algebra is reintroduced (when the spelling-map writer comes back),
typing "z" should fuzz-match "zh"-prefixed syllables, etc. For now
the MVP IME accepts only exact-spelled syllables.

Usage:
  python build_dict.py <luna_pinyin.dict.yaml> <output_basename>
                       [--preset <essay.txt> [--preset-min-weight N]]

Example:
  python build_dict.py src/librime/data/minimal/luna_pinyin.dict.yaml \\
                       data/luna_pinyin \\
                       --preset src/librime/data/minimal/essay.txt \\
                       --preset-min-weight 500

Produces data/luna_pinyin.prism.bin and data/luna_pinyin.table.bin.

Preset vocabulary: the upstream essay.txt format is just "<phrase>\\t<weight>"
with NO pinyin codes -- the IME is supposed to derive them at runtime from
the character->reading mapping in essay.txt + a separate dict. We do the
derivation at build time instead: scan luna_pinyin.dict.yaml's character
entries to learn each char's most-frequent reading, then for each essay
phrase whose weight meets the threshold, look up each char's reading and
emit a multi-syllable entry. Polyphones use the highest-weighted reading.
Phrases containing chars not in the dict are skipped.
"""

import argparse
import os
import struct
import sys


# --------------------------------------------------------------------------
# Constants -- must match prism.h / table.h / string_table.cc on the C++ side.
# --------------------------------------------------------------------------

PRISM_FLAT_FORMAT = b"Rime::PrismFlat/1.0"
TABLE_FORMAT = b"Rime::Table/4.0"

STRING_TABLE_MAGIC = b"RST1"

# Code::kIndexCodeMaxLength in vocabulary.h. Phrases longer than this are
# stored as LongEntry under a tail index keyed at level kIndexCodeMaxLength.
INDEX_CODE_MAX_LENGTH = 3

# struct sizes (bytes), 4-byte alignment, x86 / ARM consistent.
SIZE_METADATA_TABLE = 68    # Metadata in table.h
SIZE_METADATA_PRISM_FLAT = 48  # FlatMetadata in prism.h (32 + 4*4)
SIZE_STRINGTYPE = 4         # union of String OffsetPtr (4) and StringId (4)
SIZE_ENTRY = 8              # StringType + float weight
SIZE_LIST = 8               # size (uint32) + OffsetPtr (int32)
SIZE_HEAD_INDEX_NODE = 12   # List<Entry> entries (8) + OffsetPtr (4)
SIZE_TRUNK_INDEX_NODE = 16  # SyllableId (4) + List<Entry> (8) + OffsetPtr (4)
SIZE_LONG_ENTRY = 16        # Code (List<SyllableId> = 8) + Entry (8)


# --------------------------------------------------------------------------
# BinBuilder: linear byte buffer with OffsetPtr backpatching.
#
# OffsetPtr semantics: the on-disk value is `target_offset - field_offset`
# (an int32). Zero means NULL. We collect (field_offset, target_offset)
# pairs during emission and resolve them in finalize().
# --------------------------------------------------------------------------

class BinBuilder(object):
    def __init__(self):
        self.buf = bytearray()
        self.patches = []  # list of (field_off, target_off) absolute offsets

    def alloc(self, nbytes):
        off = len(self.buf)
        self.buf.extend(b"\0" * nbytes)
        return off

    def append(self, data):
        off = len(self.buf)
        self.buf.extend(data)
        return off

    def write_u32(self, off, val):
        struct.pack_into("<I", self.buf, off, val)

    def write_i32(self, off, val):
        struct.pack_into("<i", self.buf, off, val)

    def write_f32(self, off, val):
        struct.pack_into("<f", self.buf, off, val)

    def write_bytes(self, off, data):
        self.buf[off : off + len(data)] = data

    def write_fixed_string(self, off, s, max_len):
        """Null-terminated string, padded with zeros to max_len."""
        b = s.encode("utf-8") if isinstance(s, str) else s
        assert len(b) < max_len, "fixed string %r exceeds %d" % (s, max_len)
        self.buf[off : off + len(b)] = b
        # remaining bytes already zero from alloc()

    def add_patch(self, field_off, target_off):
        """Record that an OffsetPtr at field_off should resolve to target_off
        (both absolute byte offsets in the final buffer)."""
        if target_off == 0:
            return  # leave field as zero -> NULL
        self.patches.append((field_off, target_off))

    def finalize(self):
        for field_off, target_off in self.patches:
            self.write_i32(field_off, target_off - field_off)
        return bytes(self.buf)


# --------------------------------------------------------------------------
# String table builder. Format ("RST1"): magic + count + (len, bytes)*.
# Insertion order is preserved. Returns (id, blob_bytes).
# --------------------------------------------------------------------------

class StringTableBuilder(object):
    def __init__(self):
        self.keys = []           # in insertion order
        self.index = {}          # string -> id

    def add(self, s):
        if s in self.index:
            return self.index[s]
        i = len(self.keys)
        self.keys.append(s)
        self.index[s] = i
        return i

    def lookup(self, s):
        return self.index.get(s, None)

    def serialize(self):
        out = bytearray()
        out += STRING_TABLE_MAGIC
        out += struct.pack("<I", len(self.keys))
        for k in self.keys:
            kb = k.encode("utf-8")
            out += struct.pack("<I", len(kb))
            out += kb
        return bytes(out)


# --------------------------------------------------------------------------
# YAML dict reader. Parses only what we need from a Rime .dict.yaml:
#   * skip the front-matter (--- ... ...)
#   * each remaining non-blank line is "text<TAB>code[<TAB>weight]"
#   * code is space-separated syllables; weight defaults to 0.0
# Comments starting with '#' are skipped. Blank lines too.
# --------------------------------------------------------------------------

class DictEntry(object):
    __slots__ = ("text", "code", "weight")

    def __init__(self, text, code, weight):
        self.text = text
        self.code = code            # tuple of syllable strings
        self.weight = weight        # float


def read_dict_yaml(path):
    entries = []
    syllables = set()
    in_body = False
    saw_front_matter_open = False
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not in_body:
                stripped = line.strip()
                if stripped == "---":
                    saw_front_matter_open = True
                    continue
                if stripped == "...":
                    in_body = True
                    continue
                # Lines before the front matter open, or inside the
                # front matter, are ignored.
                continue
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            text = parts[0].strip()
            code_str = parts[1].strip()
            weight = 0.0
            if len(parts) >= 3:
                try:
                    weight = float(parts[2].strip())
                except ValueError:
                    weight = 0.0
            code = tuple(s for s in code_str.split() if s)
            if not text or not code:
                continue
            entries.append(DictEntry(text, code, weight))
            for s in code:
                syllables.add(s)
    if not saw_front_matter_open:
        # Tolerate a dict without a front-matter -- treat everything as body.
        pass
    return entries, syllables


# --------------------------------------------------------------------------
# Preset vocabulary reader: ingests an essay.txt-style file ("phrase\tweight"
# with no pinyin codes). Char readings are derived from the main dict.
#
# We build char->reading by scanning dict entries where character count ==
# syllable count: each char gets credit for its pinyin weighted by the
# entry's weight (with a +1 floor so weight-0 entries still count). The
# most-credited reading wins -- this naturally picks the common reading
# for polyphones like 行 (xing vs hang) since the common reading appears
# in far more dict entries.
# --------------------------------------------------------------------------

def build_char_pinyin_map(entries):
    """Return dict: char -> chosen pinyin reading."""
    scores = {}  # char -> {pinyin -> total weight}
    for e in entries:
        chars = list(e.text)
        if len(chars) != len(e.code):
            continue
        w = e.weight if e.weight > 0 else 1.0
        for ch, py in zip(chars, e.code):
            slot = scores.get(ch)
            if slot is None:
                slot = {}
                scores[ch] = slot
            slot[py] = slot.get(py, 0.0) + w
    chosen = {}
    for ch, slot in scores.items():
        best_py = None
        best_w = -1.0
        for py, w in slot.items():
            if w > best_w:
                best_w = w
                best_py = py
        chosen[ch] = best_py
    return chosen


def read_preset_vocabulary(path, char_map, min_weight, max_phrase_len=8):
    """Read essay.txt; return (entries, stats). Entries with chars not in
    char_map are skipped. Single-char entries are skipped (already in
    main dict)."""
    out = []
    n_total = 0
    n_below_weight = 0
    n_too_long = 0
    n_too_short = 0
    n_unknown_char = 0
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            phrase = parts[0].strip()
            weight = 0.0
            if len(parts) >= 2:
                try:
                    weight = float(parts[1].strip())
                except ValueError:
                    weight = 0.0
            if not phrase:
                continue
            n_total += 1
            if weight < min_weight:
                n_below_weight += 1
                continue
            chars = list(phrase)
            if len(chars) < 2:
                n_too_short += 1
                continue
            if len(chars) > max_phrase_len:
                n_too_long += 1
                continue
            code = []
            missing = False
            for ch in chars:
                py = char_map.get(ch)
                if py is None:
                    missing = True
                    break
                code.append(py)
            if missing:
                n_unknown_char += 1
                continue
            out.append(DictEntry(phrase, tuple(code), weight))
    stats = {
        "total_lines": n_total,
        "kept": len(out),
        "below_weight": n_below_weight,
        "too_long": n_too_long,
        "too_short": n_too_short,
        "unknown_char": n_unknown_char,
    }
    return out, stats


# --------------------------------------------------------------------------
# Vocabulary tree (mirrors rime/dict/vocabulary.h: Vocabulary = map<int,
# VocabularyPage>, page has entries + next_level). Keys are syllable IDs.
# Index keys are code[0]/code[1]/...; for codes longer than
# kIndexCodeMaxLength, the tail bucket is keyed by -1.
# --------------------------------------------------------------------------

class VocabularyPage(object):
    __slots__ = ("entries", "next_level")

    def __init__(self):
        self.entries = []        # list of (text, code_ids, weight)
        self.next_level = None   # VocabularyPage map OR None


def build_vocabulary(entries, syll_to_id):
    """Build the multi-level Vocabulary map. Mirrors
    Vocabulary::LocateEntries in vocabulary.cc."""
    root = {}    # syllable_id -> VocabularyPage

    def locate(code_ids):
        """Walk the tree; return the page where entries for this code
        should live."""
        node_map = root
        n = len(code_ids)
        i = 0
        while True:
            # Mirrors:
            #   key = (i < kIndexCodeMaxLength) ? code[i] : -1
            key = code_ids[i] if i < INDEX_CODE_MAX_LENGTH else -1
            page = node_map.get(key)
            if page is None:
                page = VocabularyPage()
                node_map[key] = page
            # Mirrors: "if (i == n - 1 || i == kIndexCodeMaxLength) return"
            if i == n - 1 or i == INDEX_CODE_MAX_LENGTH:
                return page
            if page.next_level is None:
                page.next_level = {}
            node_map = page.next_level
            i += 1

    for e in entries:
        ids = tuple(syll_to_id[s] for s in e.code)
        page = locate(ids)
        page.entries.append((e.text, ids, e.weight))

    return root


# --------------------------------------------------------------------------
# Build a flat-mode .prism.bin. Layout:
#
#   offset 0..47:   FlatMetadata
#     0..31:        format[32]   = "Rime::PrismFlat/1.0\0..."
#     32..35:       dict_file_checksum
#     36..39:       num_syllables
#     40..43:       string_table_offset (absolute)
#     44..47:       string_table_size
#   offset 48..:    string table blob (RST1 + sorted syllables)
# --------------------------------------------------------------------------

def build_prism_flat(syllables_sorted, dict_checksum):
    b = BinBuilder()
    meta_off = b.alloc(SIZE_METADATA_PRISM_FLAT)

    st = StringTableBuilder()
    for s in syllables_sorted:
        st.add(s)
    st_blob = st.serialize()
    st_off = b.append(st_blob)

    b.write_fixed_string(meta_off + 0, PRISM_FLAT_FORMAT, 32)
    b.write_u32(meta_off + 32, dict_checksum)
    b.write_u32(meta_off + 36, len(syllables_sorted))
    b.write_u32(meta_off + 40, st_off)
    b.write_u32(meta_off + 44, len(st_blob))

    return b.finalize()


# --------------------------------------------------------------------------
# Build the .table.bin. Bottom-up serialisation walking the Vocabulary
# tree. See table.h for struct definitions.
#
# We emit, in order:
#   1. Metadata (reserved, fields filled in last)
#   2. Syllabary (Array<StringType>)
#   3. HeadIndex (Array<HeadIndexNode>)
#   4. All sub-arrays (TrunkIndex, TailIndex, Entry lists, LongEntry
#      lists, SyllableId arrays for extra_codes) emitted recursively.
#   5. String table blob.
#
# Within HeadIndexNode/TrunkIndexNode/TailIndex emission, the entry-list
# data is emitted first so we know the absolute offset to patch into the
# List<Entry>::at field.
# --------------------------------------------------------------------------

def build_table_bin(syllables_sorted, vocab_root, num_entries, dict_checksum):
    b = BinBuilder()

    # Reserve metadata; we'll patch its OffsetPtr fields at the end.
    meta_off = b.alloc(SIZE_METADATA_TABLE)
    # offset 32: dict_file_checksum, 36: num_syllables, 40: num_entries,
    # 44: syllabary OffsetPtr, 48: index OffsetPtr,
    # 52/56: reserved, 60: string_table OffsetPtr, 64: string_table_size.

    # The on-disk string table dedups syllable strings AND entry texts.
    # Insertion order = id, which is what Table::GetString relies on
    # (str_id -> string).
    st = StringTableBuilder()
    # Pre-allocate ids for syllables first so they keep low ids -- not
    # required for correctness, but helps debugging dumps.
    for s in syllables_sorted:
        st.add(s)

    # Pre-walk: collect all entry text ids by adding to the StringTable.
    def walk_for_strings(node_map):
        if node_map is None:
            return
        for syl_id in sorted(node_map.keys()):
            page = node_map[syl_id]
            for (text, _ids, _w) in page.entries:
                st.add(text)
            walk_for_strings(page.next_level)
    walk_for_strings(vocab_root)

    # ----------------------------------------------------------------
    # Emit a List<Entry> at the current buffer end and return (list_off,
    # at_off, count). The caller writes (list_off, at_off, count) into a
    # parent List<Entry> field structure.
    # ----------------------------------------------------------------
    def emit_entry_array(entries):
        """Emit `count` Entry records packed; return their array start
        offset. Entries: list of (text, _code, weight)."""
        if not entries:
            return 0
        first_off = b.alloc(SIZE_ENTRY * len(entries))
        for i, (text, _code, weight) in enumerate(entries):
            e_off = first_off + i * SIZE_ENTRY
            str_id = st.lookup(text)
            assert str_id is not None
            # StringType { int32_t value; } -- we use the StringId branch.
            b.write_i32(e_off + 0, str_id)
            b.write_f32(e_off + 4, weight)
        return first_off

    # ----------------------------------------------------------------
    # Emit a LongEntry array (tail entries with extra_code beyond
    # kIndexCodeMaxLength). Each LongEntry: Code (List<SyllableId>) +
    # Entry. The List's "at" points to an array of SyllableId allocated
    # before the LongEntry struct.
    # ----------------------------------------------------------------
    def emit_tail_index(entries):
        """entries: list of (text, code_ids, weight) where len(code_ids)
        > kIndexCodeMaxLength. Returns the TailIndex absolute offset
        (= the Array<LongEntry> struct: uint32 size + LongEntry[size])."""
        # First write the extra_code SyllableId arrays for each entry.
        extra_code_offs = []
        for (_text, code_ids, _w) in entries:
            extra = code_ids[INDEX_CODE_MAX_LENGTH:]
            if not extra:
                extra_code_offs.append((0, 0))
                continue
            arr_off = b.alloc(4 * len(extra))
            for i, sid in enumerate(extra):
                b.write_i32(arr_off + i * 4, sid)
            extra_code_offs.append((arr_off, len(extra)))

        # Now the TailIndex = Array<LongEntry>: size (4) + LongEntry[n].
        tail_off = b.alloc(4 + SIZE_LONG_ENTRY * len(entries))
        b.write_u32(tail_off, len(entries))
        base = tail_off + 4
        for i, ((text, code_ids, weight), (arr_off, n)) in enumerate(
                zip(entries, extra_code_offs)):
            le_off = base + i * SIZE_LONG_ENTRY
            # LongEntry { Code extra_code (List); Entry entry; }
            # Code = List<SyllableId> { uint32 size; OffsetPtr at; }
            b.write_u32(le_off + 0, n)            # extra_code.size
            b.add_patch(le_off + 4, arr_off)      # extra_code.at -> arr_off
            # Entry { StringType text; float weight; }
            str_id = st.lookup(text)
            assert str_id is not None
            b.write_i32(le_off + 8, str_id)
            b.write_f32(le_off + 12, weight)
        return tail_off

    # ----------------------------------------------------------------
    # Emit a TrunkIndex at level >= 1. node_map is the Vocabulary
    # subtree at a given prefix. Returns offset to Array<TrunkIndexNode>.
    # ----------------------------------------------------------------
    def emit_trunk_index(node_map, prefix_len):
        """node_map keys: SyllableId. For each, emit a TrunkIndexNode.
        prefix_len = current depth so far (1, 2, ...). When prefix_len+1
        reaches kIndexCodeMaxLength, the next level becomes a tail
        index for codes that overflow."""
        # Excludes the -1 tail bucket (those go into TailIndex of parent).
        keys = sorted(k for k in node_map.keys() if k != -1)

        # First, recursively emit each node's child sub-index and entry
        # array. Save the offsets to fill into the TrunkIndexNode struct.
        node_payloads = []
        for k in keys:
            page = node_map[k]
            entries_off = emit_entry_array(page.entries)

            next_off = 0
            if page.next_level:
                next_depth = prefix_len + 1
                if next_depth < INDEX_CODE_MAX_LENGTH:
                    next_off = emit_trunk_index(page.next_level, next_depth)
                else:
                    # We've hit the tail boundary. Page.next_level should
                    # have a -1 page with overflow entries.
                    tail_page = page.next_level.get(-1)
                    if tail_page:
                        next_off = emit_tail_index(tail_page.entries)
            node_payloads.append((k, entries_off, len(page.entries), next_off))

        trunk_off = b.alloc(4 + SIZE_TRUNK_INDEX_NODE * len(keys))
        b.write_u32(trunk_off, len(keys))
        base = trunk_off + 4
        for i, (k, entries_off, ecount, next_off) in enumerate(node_payloads):
            n_off = base + i * SIZE_TRUNK_INDEX_NODE
            b.write_i32(n_off + 0, k)              # key
            b.write_u32(n_off + 4, ecount)         # entries.size
            b.add_patch(n_off + 8, entries_off)    # entries.at
            b.add_patch(n_off + 12, next_off)      # next_level
        return trunk_off

    # ----------------------------------------------------------------
    # Emit the HeadIndex: a dense array indexed by syllable_id 0..N-1
    # (i.e. one node per syllable in the syllabary). Each node has a
    # List<Entry> entries (for single-syllable words) and an OffsetPtr
    # to next-level TrunkIndex (for multi-syllable phrases starting
    # with this syllable).
    # ----------------------------------------------------------------
    def emit_head_index(vocab_root, num_syllables):
        node_payloads = []
        for sid in range(num_syllables):
            page = vocab_root.get(sid)
            if page is None:
                node_payloads.append((0, 0, 0))
                continue
            entries_off = emit_entry_array(page.entries)
            next_off = 0
            if page.next_level:
                # At depth 1, next is trunk OR tail depending on whether
                # we already hit kIndexCodeMaxLength (=3). Since we're at
                # depth 1 starting, next is trunk.
                if INDEX_CODE_MAX_LENGTH > 1:
                    next_off = emit_trunk_index(page.next_level, 1)
                else:
                    tail_page = page.next_level.get(-1)
                    if tail_page:
                        next_off = emit_tail_index(tail_page.entries)
            node_payloads.append((entries_off, len(page.entries), next_off))

        head_off = b.alloc(4 + SIZE_HEAD_INDEX_NODE * num_syllables)
        b.write_u32(head_off, num_syllables)
        base = head_off + 4
        for i, (entries_off, ecount, next_off) in enumerate(node_payloads):
            n_off = base + i * SIZE_HEAD_INDEX_NODE
            b.write_u32(n_off + 0, ecount)         # entries.size
            b.add_patch(n_off + 4, entries_off)    # entries.at
            b.add_patch(n_off + 8, next_off)       # next_level
        return head_off

    # ----------------------------------------------------------------
    # Emit the Syllabary: Array<StringType> sized num_syllables. Each
    # StringType holds the str_id of the syllable string in the table-
    # internal StringTable.
    # ----------------------------------------------------------------
    num_syllables = len(syllables_sorted)
    syllabary_off = b.alloc(4 + SIZE_STRINGTYPE * num_syllables)
    b.write_u32(syllabary_off, num_syllables)
    for i, syl in enumerate(syllables_sorted):
        b.write_i32(syllabary_off + 4 + i * SIZE_STRINGTYPE, st.lookup(syl))

    # Emit the index tree.
    head_off = emit_head_index(vocab_root, num_syllables)

    # Emit the string table blob.
    st_blob = st.serialize()
    st_off = b.append(st_blob)

    # Patch the metadata.
    b.write_fixed_string(meta_off + 0, TABLE_FORMAT, 32)
    b.write_u32(meta_off + 32, dict_checksum)
    b.write_u32(meta_off + 36, num_syllables)
    b.write_u32(meta_off + 40, num_entries)
    b.add_patch(meta_off + 44, syllabary_off)      # syllabary
    b.add_patch(meta_off + 48, head_off)           # index
    # offsets 52, 56: reserved (leave zero)
    b.add_patch(meta_off + 60, st_off)             # string_table
    b.write_u32(meta_off + 64, len(st_blob))

    return b.finalize()


# --------------------------------------------------------------------------
# Top-level glue.
# --------------------------------------------------------------------------

def crc32_of_file(path):
    """Just a stable mixing of the input bytes. Doesn't have to be a
    true CRC -- Dictionary doesn't validate the checksum on load."""
    import zlib
    with open(path, "rb") as f:
        return zlib.crc32(f.read()) & 0xffffffff


def main(argv):
    p = argparse.ArgumentParser(
        description="Build .prism.bin and .table.bin from a Rime dict.yaml")
    p.add_argument("dict_yaml",
                   help="path to luna_pinyin.dict.yaml (or similar)")
    p.add_argument("out_base",
                   help="output basename (will produce .prism.bin "
                        "and .table.bin)")
    p.add_argument("--preset", default=None,
                   help="optional essay.txt-style preset vocabulary "
                        "(phrase\\tweight, no pinyin)")
    p.add_argument("--preset-min-weight", type=float, default=500.0,
                   help="skip preset entries with weight below this "
                        "(default 500)")
    args = p.parse_args(argv[1:])

    in_path = args.dict_yaml
    out_base = args.out_base

    entries, syllable_set = read_dict_yaml(in_path)
    if not entries:
        print("error: no entries parsed from %s" % in_path, file=sys.stderr)
        sys.exit(1)
    print("read %d entries, %d syllables from %s"
          % (len(entries), len(syllable_set), in_path))

    if args.preset:
        char_map = build_char_pinyin_map(entries)
        print("derived %d char->reading mappings from main dict"
              % len(char_map))
        preset_entries, stats = read_preset_vocabulary(
            args.preset, char_map, args.preset_min_weight)
        print("preset %s: kept %d / %d lines (below_weight=%d, "
              "too_short=%d, too_long=%d, unknown_char=%d)"
              % (args.preset, stats["kept"], stats["total_lines"],
                 stats["below_weight"], stats["too_short"],
                 stats["too_long"], stats["unknown_char"]))
        entries.extend(preset_entries)
        # Preset entries only use syllables already in the main dict
        # (char_map values come from dict entries), so syllable_set is
        # unchanged. Assert this to catch regressions.
        for e in preset_entries:
            for s in e.code:
                assert s in syllable_set, \
                    "preset syllable %r not in main syllabary" % s

    syllables_sorted = sorted(syllable_set)
    syll_to_id = {s: i for i, s in enumerate(syllables_sorted)}

    vocab = build_vocabulary(entries, syll_to_id)

    checksum = crc32_of_file(in_path)

    prism_bytes = build_prism_flat(syllables_sorted, checksum)
    table_bytes = build_table_bin(syllables_sorted, vocab,
                                  len(entries), checksum)

    out_dir = os.path.dirname(out_base)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    prism_path = out_base + ".prism.bin"
    table_path = out_base + ".table.bin"
    with open(prism_path, "wb") as f:
        f.write(prism_bytes)
    with open(table_path, "wb") as f:
        f.write(table_bytes)

    print("wrote %s (%d bytes, %d syllables)"
          % (prism_path, len(prism_bytes), len(syllables_sorted)))
    print("wrote %s (%d bytes, %d entries)"
          % (table_path, len(table_bytes), len(entries)))


if __name__ == "__main__":
    main(sys.argv)
