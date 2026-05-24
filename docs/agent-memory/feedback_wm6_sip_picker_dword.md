---
name: WM6 SIP picker only enumerates DWORD IsSIPInputMethod
description: Why "WMRime" didn't show in the SIP list the first time, and the registry-type fix
type: feedback
---
On Windows Mobile 6, the SIP picker enumerates `HKLM\Software\Microsoft\Shell\Keybd\<CLSID>\` and **only lists entries where `IsSIPInputMethod` is `REG_DWORD` with value 1**. A `REG_SZ` `"1"` is silently rejected -- no error, just absent from the picker.

This bit us once: the first WMRime.inf wrote
```
HKLM,"...","IsSIPInputMethod",0,"1"    ; flag 0 = REG_SZ
```
and the installed CAB looked fine, registry value was there, but the SIP never showed up. The fix:
```
HKLM,"...","IsSIPInputMethod",0x00010001,1   ; flag 0x00010001 = REG_DWORD
```

**Why:** The shell filter does a strict type check; only DWORD-typed `IsSIPInputMethod==1` passes.

**How to apply:**
- In INF, `IsSIPInputMethod` MUST use flag `0x00010001` (REG_DWORD), unquoted integer value.
- In code (`DllRegisterServer`), MUST use `RegSetValueExW(..., REG_DWORD, ...)`, not REG_SZ.
- Symptom: CAB installs cleanly, no errors, files land in place, but Settings > Input doesn't show your SIP. Verify via on-device registry editor: the value's "Type" column should read "DWORD" not "String".
- Related companions worth setting on the same key: `Name` (REG_SZ, display name) and `PreferredImage` (REG_DWORD, optional icon-resource index; 0 = none).

Also worth mirroring CLSID registration under both `HKLM\Software\Classes\CLSID\` and `HKCR\CLSID\` -- some WinCE COM consumers query HKCR directly, and the WinCE 6 HKCR-as-alias behavior is build-dependent.
