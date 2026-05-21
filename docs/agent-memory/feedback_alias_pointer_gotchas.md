---
name: wince_compat smart-pointer alias gotchas
description: C++03 constraints that recur when porting upstream librime to an<>/the<>/of<>/weak<>
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
The `an<T>` / `the<T>` / `of<T>` / `weak<T>` aliases in `rime/common.h` are C++03 class-template wrappers around `wince::shared_ptr<T>` (and `weak_ptr<T>`), not C++11 template aliases. This subtly changes the rules from upstream's `using` aliases. Two recurring gotchas:

**1. No raw-pointer ctor on the alias wrappers.**
`wince::shared_ptr<T>` has `explicit shared_ptr(U* p)` but `an<T>` inherits only the copy / converting ctors, not the explicit raw-pointer one. So `member_(new Foo(args))` in a constructor mem-initialiser list will FAIL to compile with C2664 ("cannot convert from `Foo*` to `const shared_ptr<T>&`"). The replacement is `member_(New<Foo>(args))`, which routes through `wince::make_shared` and works for any of the alias wrappers because they all accept `const shared_ptr<U>&`.

**2. Sibling aliases cannot bind references to each other.**
**UPDATE 2026-05-19**: `of<T>` was REFACTORED to inherit from `an<T>` (mirrors upstream `using of = an<T>;` where they're literally the same type). So `an<T>& x = some_of_T;` now WORKS via derived-to-base reference binding. The gotcha still applies to genuinely-sibling pairs (e.g. `an<T>` vs `the<T>`), but `of`/`an` no longer trigger it. If you see C2440 on these specifically, you may be on an older common.h.

For the still-sibling pairs (`the<T>` vs `an<T>`), the original workarounds apply:
  * Loop with the actual stored type.
  * Or bind to a `wince::shared_ptr<T>&` (the common base -- implicit derived-to-base reference binding is allowed).
  * Or copy by value: `an<T> x = *it;` (cheap, refcount bump).

**Why:** Hit both issues during the menu/translation/algebra ports (2026-05-18). The error messages from MSVC 9.0 point at the wrong end (C2664 blames the rhs raw pointer; C2440 blames the lhs reference type) and are easy to misdiagnose as missing includes. Saving them so the next port pass doesn't re-derive the fix.

**How to apply:** When you see C2664 on a member initialiser or argument-passing site involving an alias wrapper, suspect explicit-ctor mismatch first. When you see C2440 on `T& x = *iter` where `iter` is into a container of a sibling alias, suspect the type mismatch and fix the loop variable type rather than the iterator.
