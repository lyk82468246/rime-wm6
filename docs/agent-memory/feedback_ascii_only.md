---
name: WinCE port — keep source files ASCII-only
description: MSVC9 on Chinese-locale Windows reads UTF-8 files as CP936 and warns C4819 on every non-ASCII byte; stick to ASCII in comments
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
All source files for the WinCE port (`src/wince_compat/*`, `src/librime_wince/**`, `rime-wm6/**/*.cc`) must be pure ASCII.

**Why:** The build machine runs Chinese-locale Windows, so MSVC 9.0 defaults its source code page to 936 (GBK). Without a UTF-8 BOM, files written as UTF-8 trigger `warning C4819: this file contains characters that cannot be represented in the current code page (936)`. Worse, the parser may misinterpret multi-byte sequences inside comments, leading to confusing knock-on errors (we burned three build cycles in 2026-05 chasing a phantom `DllMain` C2731 that may have been correlated with stray em-dashes).

**How to apply:**

- Write `--` instead of em-dash `—` (U+2014).
- Write `->` instead of `→` (U+2192), `<-` instead of `←`, `<->` instead of `↔`.
- Use straight ASCII quotes `"..."` `'...'`, not smart quotes `"..."` `'...'`.
- Write `...` (three periods) instead of `…` (U+2026).
- For Chinese text in comments, prefer English instead. If Chinese is unavoidable, save the file with a UTF-8 BOM (3 bytes `EF BB BF` at start) so MSVC9 switches modes.
- Before committing, optionally grep: `python -c "import sys; [print(p) for p in sys.argv[1:] if any(b>=0x80 for b in open(p,'rb').read())]" src/wince_compat/*.h src/librime_wince/**/*.h`
- The MEMORY index `MEMORY.md` and other files under `~/.claude/projects/.../memory/` are NOT subject to this rule — they're not compiled by MSVC9.
