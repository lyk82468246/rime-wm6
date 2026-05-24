---
name: Never run dev-only smoke tests during DLL load on real hardware
description: The RimeCore SmokeTestRunner global ran 40 exercises at DLL_PROCESS_ATTACH; one of them blew up on-device and produced ERROR_DLL_INIT_FAILED (1114) -- which surfaced as "WMRime appears in SIP picker but selecting reverts to default"
type: feedback
---

`rime-wm6/RimeCore/dllmain.cc` originally had a namespace-scope global `SmokeTestRunner g_smoke_runner;` whose constructor ran ~40 `exercise_*()` helpers (YAML parse, regex compile, Service singleton init, Engine creation, dict layer probes, etc.) at module load. The intent was to anchor the wince_compat shim instantiations so MSVC9 would emit any template diagnostics at build time, not link time.

On real WM6 hardware this caused `LoadLibrary(RimeCore.dll)` to fail with **ERROR_DLL_INIT_FAILED (1114)**. WMRimeSIP.dll statically imports RimeCore, so its `LoadLibrary` also returned 1114. The shell-side SIP picker still enumerated WMRime (registry-only), but selecting it immediately reverted to the default keyboard because `CoCreateInstance` couldn't load the in-proc server.

**Why:** Any one of the 40 exercises throwing an exception, hitting an SEH fault, or simply touching a coredll export missing on the user's WM6 build is enough to kill the entire DLL load. On the dev box the same exercises all pass because the desktop CRT / SDK is forgiving; on the device the surface area for failures is much larger.

**How to apply:**
- **Never put smoke tests / dev-time verifications behind a namespace-scope global** in a DLL that will ship to a device. Move them into a `#ifdef X_SMOKE_TEST` block, a separate "verify.exe" binary, or a test that runs only when `RimeInitialize` is called.
- When LoadLibrary returns 1114 on a WinCE/WM6 device, the dependent DLLs are fine and the architecture is fine -- the C runtime's static-initializer loop returned FALSE because one of YOUR globals' constructors blew up. Look for namespace-scope `T name(args);` lines in the failing DLL's sources.
- The diagnostic loop here was: probe.exe -> LoadLibrary returns 1114 -> grep for namespace-scope globals with non-trivial ctors -> remove or gate them.
- Symptom of this bug: SIP shows in `Settings > Personal > Input` (registry only), but selecting it falls back to default (DLL load failed). The "no log file written" signal confirms the SIP DLL's static init never reached our `SipBootstrapper g_bootstrapper` line, which is consistent with CRT-init returning FALSE before any user code ran.

Companion: [sip-registration-journey.md](../sip-registration-journey.md) for the registry-half of the saga; this note covers the DLL-load-half.
