---
name: PinyinTranslator MVP shape
description: Why we wrote our own minimal PinyinTranslator instead of porting upstream's 704-line script_translator
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
`src/librime_wince/src/rime/gear/pinyin_translator.{h,cc}` is a deliberately minimal MVP: ~120 lines vs upstream `script_translator.cc` at 704 lines.

**Skipped on purpose** (each defers a whole subsystem):
- `Memory` base + `UserDictionary` (learning, commit history persistence)
- `Poet` (sentence/grammar scoring across segments)
- `TranslatorOptions` from `gear/translator_commons.h` (preedit formatting, comment style, spelling hints)
- `Corrector` (typo tolerance — we have a stub but don't wire it in)
- predict_word, blacklist, sentence composition

**What it does:** Query() runs Syllabifier->BuildSyllableGraph, calls Dictionary::Lookup, walks DictEntryCollector in reverse-key order (longest match first), wraps each DictEntry as a SimpleCandidate, returns FifoTranslation.

**Why:** Want to land Dictionary->Translator wiring this batch. The full script_translator drags in Memory, Poet, UserDictionary, ResourceResolver — each its own port project. Once those land, decide whether to grow PinyinTranslator into a script_translator port or keep PinyinTranslator as the lite path and add ScriptTranslator alongside.

**How to apply:** When the user asks about translator features (learning, sentence-mode, custom phrases, grammar), they're asking for things we deliberately left out. Don't act surprised they're missing; point them at this trade-off and ask whether to expand PinyinTranslator or kick off the full ScriptTranslator port.
