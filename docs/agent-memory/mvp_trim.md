---
name: MVP feature trim scope
description: Features cut from first viable WinCE port to bound complexity; PC-side preprocessing replaces on-device runtime
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
To make the first working build feasible, ruthlessly cut the following librime features. Each cut is justified by either platform infeasibility or PC-side substitution.

**Why:** User confirmed on 2026-05-17 the project starts with nothing of value in pwd, so aggressive scope reduction is the only path to a runnable artifact within reasonable time.

**How to apply:**

| Feature cut | Reason | Substitution |
|---|---|---|
| OpenCC (简繁转换) | Heavy dict files, too much RAM | None — user accepts no S/T conversion |
| LevelDB + user_db | mmap + file locks broken on WinCE; user word learning needs writable storage | Read-only prebuilt dictionaries only; no user learning |
| dict_compiler | Builds prism/table from YAML on-device, too slow + needs YAML | PC-side: precompile to .prism.bin / .table.bin, ship binary to device |
| yaml-cpp | Heavy C++17 code; schemas only read at boot | PC-side: convert all YAML schemas to custom binary format; device only deserializes |
| glog | Heavy, threading-dependent | Use existing src/librime/src/rime/no_logging.h stub (already in tree) |
| Threading | <thread>/<mutex> absent in MSVC9; CRITICAL_SECTION available but adds complexity | `#define RIME_NO_THREADING` — deploy runs synchronously on UI thread (device never deploys anyway) |
| Timestamps | <chrono> absent | `#define RIME_NO_TIMESTAMP` |
| boost::signals2 | Massive template metaprogramming, won't compile in MSVC9 | wince_compat/signal.h — minimal callback list |
| boost::unordered_* | Won't compile in MSVC9 | std::map / std::set (red-black tree, perf acceptable for IME workload) |

Kept (with porting effort):
- marisa-trie — self-contained patricia trie (~5000 lines, mostly C++03-clean). Try compiling as-is first.
- core algo (segmentation, syllabifier, calculus, encoder)
- core gear (speller, key_binder, navigator, punctuator, script_translator)
- minimal config loader reading the precompiled binary schema format
