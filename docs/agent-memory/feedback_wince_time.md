---
name: WinCE missing libc time() — wrap GetTickCount
description: WinCE 6 SDK declares time() in <time.h> but coredll.lib doesn't export it; provide our own definition in wince_compat/time.cc
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
On WinCE 6 + MSVC 9.0, including `<time.h>` and calling `time(NULL)` compiles fine (the SDK declares the prototype) but fails at link time with:

```
LNK2019: unresolved external symbol "time"
```

because `coredll.lib` does not export `time()`. WinCE has `GetTickCount()` (DWORD, milliseconds since boot) and `SYSTEMTIME`/`FILETIME` but no UNIX-epoch wall clock function exposed to C.

**How to apply:** Add `src/wince_compat/time.cc` providing `extern "C" time_t time(time_t* out)` that wraps `GetTickCount() / 1000`. The result is monotonic seconds-since-boot, NOT a real UNIX epoch — fine for stale-session reaping (the only MVP consumer) but unsuitable for anything that needs wall-clock semantics or persistence across runs.

When yaml-cpp / deployer return and need real timestamps (e.g. `last_build_time` in a schema cache), upgrade `time.cc` to use `GetLocalTime()` + manual `SYSTEMTIME` -> `time_t` conversion. Keep the prototype the same so callers don't change.

**Why:** Hit on 2026-05-19 when `Session::Activate()` in service.cc called `time(NULL)` to stamp `last_active_time_`. Both Activate and CleanupStaleSessions use it. Replacing with a wrapper was 4 lines and unblocked the whole engine + service smoke test.
