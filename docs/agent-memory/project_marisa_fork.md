---
name: marisa-trie dependency forks the dict port
description: Decided 2026-05-19 -- hand-rolled vector<string> StringTable stub for A phase; marisa-trie vendor deferred to B phase
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
The dict layer port hit a fork at `dict/table.h` because table.h includes `string_table.h` which `#include <marisa.h>` -- the marisa-trie library. It's referenced as a git submodule under `src/librime/deps/marisa-trie/` but that folder is empty in our checkout.

**Decision (2026-05-19, user-greenlit):** Two-phase plan.
- **A phase (DONE):** Hand-rolled `vector<string>`-backed StringTable in `src/librime_wince/src/rime/dict/string_table.h/.cc`. Public API identical to upstream marisa version (HasKey/Lookup/CommonPrefixMatch/Predict/GetString/Builder::Add+Build+Dump). Custom .bin format with magic "RST1" -- NOT compatible with upstream `.table.bin`. Lets table.cc + dictionary.cc compile end-to-end and exercises the full surface in smoke tests.
- **B phase (later batch):** Vendor marisa-trie from https://github.com/s-yata/marisa-trie, port to MSVC9, swap the stub. Public API stays stable so table.cc / dictionary.cc don't change. Once swapped we can read upstream `.table.bin` files.

**What's ported and clean (as of 2026-05-19):**
- `mapped_file.h/.cc` -- Win32 mmap (read-only path)
- `vocabulary.h/.cc` -- pure data types
- `prism.h/.cc` + `darts.h` -- double-array trie
- `string_table.h/.cc` -- STUB (A phase)
- `corrector.h` -- STUB (correction disabled in MVP)
- `algo/syllabifier.h/.cc` -- ported with correction branch dead-code'd
- `table.h/.cc` -- ported, read path tested compile-time
- `dictionary.h/.cc` -- ported; DictionaryComponent::Create stubbed pending ResourceResolver
- New helper: `CreateDictionary(name, prism_path, table_path)` bypasses Component flow

**What's still blocked for end-to-end on-device demo:**
- Need a desktop conversion script: upstream luna_pinyin.dict.yaml -> our stub .prism.bin + .table.bin. Format is documented in mapped_file.h/.cc (we own it). Probably a 200-line Python script.
- OR hand-build an in-memory Vocabulary tree (no .bin files at all) and pass to a custom Translator. Simpler for first MVP demo but requires writing a Translator that reads from Dictionary.

**Important architectural fix made during port (2026-05-19):**
- `of<T>` originally inherited from `wince::shared_ptr<T>` as a sibling to `an<T>`. This broke `vector<of<T> >::operator[]` -> `an<T>&` reference binding used by Dictionary::primary_table(). Changed `of<T>` to inherit from `an<T>` (mirrors upstream `using of = an<T>`). All existing usages still compile because `of<T>` is-a `an<T>` is-a `wince::shared_ptr<T>`.
