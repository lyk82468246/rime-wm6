---
name: Desktop dict-build workflow
description: How to convert upstream luna_pinyin.dict.yaml into the WinCE port's .prism.bin + .table.bin and verify on host
type: reference
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
The `tools/` directory contains the desktop side of the dict pipeline. Workflow (run from repo root):

**Build .bin files from upstream YAML:**
```
python tools/build_dict.py src/librime/data/minimal/luna_pinyin.dict.yaml data/luna_pinyin
```

Produces `data/luna_pinyin.prism.bin` (flat-mode, ~3 KB) + `data/luna_pinyin.table.bin` (~480 KB). Algebra rules are NOT applied -- only literal syllables from the dict. Phrases longer than 3 syllables go into a tail index keyed at level 3.

**With preset vocabulary (essay.txt):**
```
python tools/build_dict.py src/librime/data/minimal/luna_pinyin.dict.yaml data/luna_pinyin \
       --preset src/librime/data/minimal/essay.txt --preset-min-weight 500
```

essay.txt is `<phrase>\t<weight>` with NO pinyin codes; the builder derives a char->reading map from the main dict (highest-weight reading wins for polyphones) and emits multi-syllable entries. essay.txt uses traditional Chinese (你好 lives there as 你好, but 中国 only as 中國 etc.). At --preset-min-weight 500 we keep ~105k of 297k lines and grow .table.bin from 480 KB to ~4 MB. Lower the threshold for more coverage at the cost of size.

**Sanity-check the headers and structure:**
```
python tools/inspect_bin.py data/luna_pinyin.prism.bin
python tools/inspect_bin.py data/luna_pinyin.table.bin
```

Dumps format string, syllable count, entry count, first 5 head-index nodes, first/last 5 strings. On a cp936-terminal Windows the Chinese text shows as 乱码 but the actual bytes are valid UTF-8.

**End-to-end lookup test (closest thing to on-device integration test):**
```
python tools/verify_dict.py data/luna_pinyin ni
python tools/verify_dict.py data/luna_pinyin yi ge jin
```

`verify_dict.py` parses the .table.bin and walks HeadIndex -> TrunkIndex (binary search) -> TailIndex exactly the way `Table::Query` does in C++. If a known code returns the right candidates here, the on-device C++ reader will get the same result.

**File formats are documented in:**
- `src/librime_wince/src/rime/dict/prism.h` -- FlatMetadata struct
- `src/librime_wince/src/rime/dict/table.h` -- Metadata + Index node structs
- `src/librime_wince/src/rime/dict/string_table.cc` -- "RST1" blob layout
- `tools/build_dict.py` -- writer reference
- `tools/verify_dict.py` -- reader reference

**Known limitations of the MVP dict (resolved later):**
- No algebra: typing "z" doesn't fuzz-match "zh"-prefix syllables. SpellingMap is NULL in flat-mode prism.
- No preset vocabulary: common multi-char phrases (你好, 中国, etc.) live in upstream's `essay-zh-hans.txt` etc., not the .dict.yaml. Loading those requires the same converter to be extended.
- Not binary-compatible with upstream `.table.bin` (we use stub StringTable instead of marisa). Will become compatible when marisa-trie is vendored (B phase per project_marisa_fork.md).

**Why this workflow:** The user picked the "Python converter" path over "vendor marisa now" because it gets us to an on-device demo faster with less heavyweight library porting. The end-to-end byte layout is verified via verify_dict.py walking the same algorithm Table::Query uses. Real device test happens once we deploy RimeCore.dll + WMRimeSIP.dll.
