---
name: rime_api MVP shape
description: How the C export surface (rime_api.{h,cc}) is wired -- MiniSession bypasses Engine for now
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
`src/librime_wince/src/rime_api.{h,cc}` is the C export surface that WMRimeSIP and any future host link against. It is a SUBSET of upstream `librime/src/rime_api.h` (594 lines -> ~150 lines).

**Architectural shortcut:** the API does NOT route through Engine/Context/Session. Instead, each session holds a `MiniSession` (input buffer + cached candidates + page state) and ProcessKey directly:
- accumulates a-z/A-Z into input, re-runs PinyinTranslator->Query
- Backspace pops, Escape clears
- Space / digit 1-9 commits highlighted-or-indexed candidate
- Page Up/Down navigates the candidate list
- Return commits raw text

The shared `PinyinTranslator` is loaded once at `RimeInitialize(traits)`, pulling `luna_pinyin.{prism,table}.bin` from `traits.shared_data_dir`.

**Why bypass Engine?** Engine's ProcessKey runs a chain of processors_/segmentors_/translators_/filters_ that come from `gear/` (ascii_composer, speller, abc_segmentor, selector, ...) -- none of which are ported. Wiring just PinyinTranslator into Engine would require dummy processor + segmentor stubs and would still bypass most of the engine. The MiniSession layer is intentionally self-contained: when gear modules land, delete MiniSession and replace rime_api.cc bodies with thin Session->Engine forwards.

**Exposed functions:** RimeSetup / RimeInitialize / RimeFinalize / RimeSetNotificationHandler / RimeCreateSession / RimeDestroySession / RimeFindSession / RimeCleanupAllSessions / RimeProcessKey / RimeCommitComposition / RimeClearComposition / RimeGetCommit / RimeFreeCommit / RimeGetContext / RimeFreeContext / RimeSelectCandidateOnCurrentPage / RimeChangePage.

**NOT exposed (subsystems not ported):** start_maintenance / deploy / sync_user_data / schema list / get_current_schema / config getters/setters/iterators / candidate list iteration beyond current page / RimeStatus.

**Memory contract:** every RimeGet* allocation must be freed with the matching RimeFree*. Strings are malloc'd UTF-8.

**How to apply:** When asked to expose a new feature to WMRimeSIP, first check whether the underlying subsystem is ported; if not, name the dependency rather than stubbing it deeply. The list above is the supported surface; everything else returns False / no-op.
