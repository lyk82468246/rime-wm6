---
name: WinCE DLL — never write a custom DllMain in MSVC9
description: VS2008 + Windows Mobile 6 SDK rejects user-written DllMain with C2731; use static initializer or RIME_MODULE_INITIALIZER instead
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
When building a DLL targeting Windows Mobile 6 Professional SDK with MSVC 9.0 (VS 2008), do NOT write a hand-rolled `DllMain` in any form. Every variant tried emits `error C2731: "DllMain": cannot overload function`, with the prior declaration pointed at our own definition's signature line — even with `extern "C"` removed, `HMODULE`/`HINSTANCE`/`HANDLE` swapped, parameter block-comments removed.

**Why:** Some WinCE SDK header forward-declares `DllMain` with a signature whose first parameter type differs (almost certainly `HANDLE`, while `HMODULE`/`HINSTANCE` are DECLARE_HANDLE'd distinct struct-pointer types). The C++ type system then sees our definition as a second overload of the same name, which is illegal for entry points.

**How to apply:**

- Just omit `DllMain` entirely. The WinCE CRT links in `_DllMainCRTStartup` which is sufficient — it returns TRUE on PROCESS_ATTACH and runs all C++ static initializers (including anything you put in the `.CRT$XCU` section).
- For module-registration that you'd normally do in DllMain, use the `RIME_MODULE_INITIALIZER(f)` macro from rime_api.h — it places a function pointer into `.CRT$XCU` so it runs at DLL load without needing DllMain.
- For an early-init hook that needs to run at attach time, declare a global object with a constructor: `namespace { struct R { R() { do_init(); } }; R g_runner; }`.
- If a future requirement absolutely needs DllMain (e.g. DLL_THREAD_DETACH cleanup), first preprocess the .cc with `cl /E` to find the conflicting forward declaration and match its exact signature.
