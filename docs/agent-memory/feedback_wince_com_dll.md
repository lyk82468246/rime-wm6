---
name: WinCE COM DLL: INITGUID + .def file + no SetPropW
description: Three traps you hit any time you write a COM DLL for WinCE 6 with MSVC9, learned while bringing up WMRimeSIP
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
When writing a COM in-proc DLL for WinCE 6 / WM6 Pro SDK with MSVC9, do these from the start, not after the build breaks:

**1. Use a `.def` file for the COM exports, not `__declspec(dllexport)`.**
`objbase.h` already declares `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer`, `DllUnregisterServer` as plain `extern "C" HRESULT STDAPICALLTYPE`. Redeclaring with `__declspec(dllexport)` triggers `error C2375: different linkage`. Wire `ModuleDefinitionFile=...\WMRimeSIP.def` in the linker config, define the four functions with plain `extern "C" HRESULT STDAPICALLTYPE`, list them in the `.def`'s EXPORTS section.

**Why:** Different `linkage` errors waste 20 minutes if you don't already know this. The .def file is the canonical fix and is what every textbook WinCE COM example does.

**How to apply:** Whenever a project will export COM entry points, create the `.def` file BEFORE writing the source. Set `ModuleDefinitionFile` in both Debug and Release configs.

---

**2. `#define INITGUID` + `#include <initguid.h>` in exactly one TU, before any header that uses `DEFINE_GUID`.**
Without this, every `DEFINE_GUID(...)` (in `clsids.h`, `sip.h`, IID definitions everywhere) emits an extern declaration but no storage. Link fails with `unresolved external CLSID_*` / `IID_*`.

Put INITGUID in `sip_main.cc` (or whichever TU owns DLL globals). Include sip.h there too so all its IIDs land in the same object file.

**Why:** Linking `uuid.lib` works for some standard COM IIDs but not for SIP-specific ones, and not for your own CLSID. INITGUID is the universal mechanism.

**How to apply:** First TU that includes objbase.h should have the INITGUID pattern. Don't sprinkle it across files -- you'll get duplicate-symbol errors.

---

**3. WinCE has no `SetPropW`/`GetPropW`/`RemovePropW`.**
The Win32 idiom for "associate a `this` pointer with an HWND" doesn't compile (`C3861: identifier not found`). Two replacements that work on WinCE:
- `SetWindowLong(GWLP_USERDATA, ...)` if you own the window and the framework doesn't already use that slot
- A process-local `std::map<HWND, MyClass*>` guarded by `CRITICAL_SECTION` -- what we ended up doing for the subclassed SIP HWND, since the SIP framework owns the window

**Why:** When subclassing a window you don't own (SIP HWND, common dialog HWND, etc.), GWLP_USERDATA may already be in use. The HWND-map is small and obviously correct.

**How to apply:** Default to the HWND-map. Reach for GWLP_USERDATA only if you own the window class.
