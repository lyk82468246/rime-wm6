---
name: MSVC9 C2027 on inline body referencing forward-decl class
description: Inline virtual function bodies in MSVC 9.0 wrongly demand the parameter type be complete, even for `(void)param;` — move bodies out of headers
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
On MSVC 9.0 (VS2008), defining a virtual method **inline in a header** with a parameter of a class type that is only forward-declared in that header will emit:

```
error C2027: used undefined type 'rime::KeyEvent'
```

pointing at the inline body even if the body never odr-uses the parameter (e.g. `(void)key_event;` or `return false;`). The standard does NOT require completeness here — this is an MSVC 9.0 quirk.

**Why:** Hit this on 2026-05-19 with `Engine::ProcessKey(const KeyEvent&)` defined inline in engine.h, where engine.h only had `class KeyEvent;`. Both engine.cc (which DID include key_event.h, but AFTER engine.h) and service.cc (which didn't include key_event.h at all) failed. Cascading C2027 also appeared inside key_event.h's own `operator<<` at the same line MSVC parsed last.

**How to apply:** When a header forward-declares a class and provides default virtual implementations of methods that take it by reference/pointer:
- Move the default bodies into the .cc file where the full header is included, OR
- Make the methods pure (`= 0`) and force subclasses to override, OR
- Include the full header from this header (last resort; bloats compile times).

The first option (move bodies to .cc) is what we used for `Engine::ProcessKey`/`ApplySchema`/`CommitText`/`Compose`. Pattern works regardless of include order in client TUs.

Do NOT confuse with a real incomplete-type error — those name a type you haven't defined at all. This bug shows up on a type that IS fully defined in another header, just not in the inline body's lookup scope.
