---
name: Mirror directory layout
description: Where ported WinCE code lives — never edit upstream src/librime/ in place; mirror at src/librime_wince/ + src/wince_compat/
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
All WinCE-specific modifications go into a mirror directory tree; upstream `src/librime/` stays pristine.

**Why:** User explicitly chose this on 2026-05-17 to preserve the ability to track and merge in future librime upstream changes. Direct in-place edits would make rebase/diff against upstream impossible.

**How to apply:**

```
src/
  librime/                 ← pristine upstream, READ ONLY (use as reference)
  librime_wince/           ← WinCE-adapted port (mirrors src/librime/ structure)
    src/
      rime/
        build_config.h     ← hand-crafted, replaces build_config.h.in
        common.h           ← rewritten for C++03
        engine.cc          ← copied + adapted from upstream
        ...
  wince_compat/            ← standalone C++03 / WinCE compatibility shim
    wince_compat.h         ← umbrella include
    shared_ptr.h           ← intrusive ref-counted smart pointer
    function.h             ← type-erased callable (a la std::function)
    signal.h               ← minimal signal/slot (replaces boost::signals2)
    path.h                 ← UTF-8 path wrapper (replaces std::filesystem::path)
    utf.h                  ← MultiByteToWideChar / WideCharToMultiByte helpers
    mutex.h                ← CRITICAL_SECTION wrapper
    chrono.h               ← GetTickCount wrapper
    ...
rime-wm6/
  RimeCore/RimeCore.vcproj ← include paths point at src/librime_wince/src + src/wince_compat
  WMRimeSIP/WMRimeSIP.vcproj
```

Rule: when porting a file like `src/librime/src/rime/engine.cc`, COPY it to `src/librime_wince/src/rime/engine.cc` first, then edit there. Never modify the upstream copy.
