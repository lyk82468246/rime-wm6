---
name: WMRimeSIP MVP shape
description: COM-DLL/SIP shell that consumes RimeCore via rime_api, plus the WinCE/MSVC9 gotchas hit while wiring it
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
`src/wmrime_sip/` is the WMRimeSIP.dll source tree -- a COM in-proc server that implements IInputMethod2 so the Windows Mobile shell picks it up as a SIP. Layout:

- `sip_main.cc` -- DLL exports (DllGetClassObject/CanUnloadNow/Register/Unregister), bootstrapper that calls RimeInitialize from registry-driven shared_data_dir, no DllMain (per `feedback_dllmain_msvc9.md`)
- `class_factory.{h,cc}` -- IClassFactory, hands out RimeInputMethod instances
- `rime_input_method.{h,cc}` -- IInputMethod2 + subclasses the SIP hwnd's WNDPROC
- `sip_window.{h,cc}` -- PanelState + RecomputeLayout / RefreshFromRime / PaintPanel / HandleTap
- `utf_conv.{h,cc}` -- UTF-8 (rime) <-> UTF-16 (Win32) via MultiByteToWideChar
- `clsids.h` -- CLSID_WMRimeSIP (generated fresh: 7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47)
- `WMRimeSIP.def` -- exports the four COM functions (avoids the linkage-redefinition issue below)

**UI model**: soft-keyboard mode. Panel = preedit strip + numbered candidate strip + 3-row QWERTY + space bar. User taps; RimeProcessKey dispatches; RimeGetCommit drains; IIMCallback::SendString pushes the text back into the focused app.

**Linking**: WMRimeSIP defines `RIMECORE_IMPORTS` so `RIME_API` becomes `__declspec(dllimport)`; links against `RimeCore.lib` from `../RimeCore/$(PlatformName)/$(ConfigurationName)`. Also links `ole32.lib`, `oleaut32.lib`. sln has an explicit ProjectDependency on RimeCore so build order is right.

## WinCE/MSVC9 gotchas hit while bringing it up

1. **No `SetPropW`/`GetPropW`/`RemovePropW`** on WinCE. Standard "store this in HWND props" pattern doesn't work. Use a process-local `std::map<HWND, RimeInputMethod*>` guarded by `CRITICAL_SECTION` instead. (Or `SetWindowLong(GWLP_USERDATA)` if you trust the SIP framework not to use it.)

2. **`DllGetClassObject` etc. are already declared in objbase.h** (no dllexport). Redeclaring with `__declspec(dllexport)` triggers `error C2375: different linkage`. Fix: use a `.def` file (`ModuleDefinitionFile=...`) and don't redeclare. The function definitions are plain `extern "C" HRESULT STDAPICALLTYPE ...`.

3. **DEFINE_GUID needs INITGUID in exactly one TU**. Without `#define INITGUID` before `<objbase.h>` and `<initguid.h>`, `CLSID_WMRimeSIP` and `IID_IInputMethod*` are unresolved externs. Put INITGUID in `sip_main.cc` only and include sip.h there to emit all the SIP IIDs in that single TU. (Alternatively link `uuid.lib`, but the INITGUID route is more self-contained.)

4. **IInputMethod2 method shape** in WM6 SDK: `SetIMMActiveContext(HWND, BOOL, DWORD, DWORD, DWORD)` is **5 args** (hwnd, bOpen, dwConversion, dwSentence, hkl), and it adds `RegisterCallback2(IIMCallback2*)`. Mis-arity gives `error C2259: cannot instantiate abstract class`.

5. **wince_compat include path**: WMRimeSIP needs `..\..\src\wince_compat` in include path too (for `stdint.h`), because `rime_api.h` includes `<stdint.h>`. Already in RimeCore's path; easy to forget for new projects.

6. **ASCII-only**: applies to wmrime_sip sources too (per `feedback_ascii_only.md`). My sip_window.h had `你 妮 泥` in an ASCII-art comment -> C4819 + downstream parse errors. Translate any Chinese in comments.

## Not yet done
- On-device test: deploy `WMRimeSIP.dll` + `RimeCore.dll` + `luna_pinyin.{prism,table}.bin` to a WM6 device, regsvrce32 the SIP, switch to "WMRime" in the SIP picker. None of this happens automatically -- needs ActiveSync / WMDC.
- Soft keyboard polish: no shift key, no number row, no symbols, no Chinese-vs-English toggle. The layout is rough block fills, not a styled keyboard.
- Page navigation buttons in the UI: currently RimeChangePage is wired but no on-panel tap target.
- IMM mode: SetIMMActiveContext is a no-op. If we ever want this SIP to also act as a hardware-keyboard IME via the IMM framework, that's a separate port.
