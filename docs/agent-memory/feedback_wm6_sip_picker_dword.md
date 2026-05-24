---
name: WM6 SIP registration is HKCR\CLSID\{guid}\IsSIPInputMethod as a SUBKEY (REG_SZ "1")
description: Authoritative Microsoft layout for registering a custom Soft Input Panel under WinCE/WM; replaces my earlier wrong guesses about HKLM\Software\Microsoft\Shell\Keybd
type: feedback
---

The WM6 / WinCE shell discovers SIPs by enumerating `HKEY_CLASSES_ROOT\CLSID\*` and picking entries whose `IsSIPInputMethod` SUBKEY has a default value of REG_SZ `"1"`. There is NO involvement of `HKLM\Software\Microsoft\Shell\Keybd\` -- that path is fictional and entries written there are silently ignored.

**Why:** Confirmed via Microsoft's own WinCE 5.0 docs ([Input Panel Registry Settings, aa452674](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/aa452674(v=msdn.10))) and Marcus Perryman's official 2005 [Custom SIP for Pocket PC](https://learn.microsoft.com/en-us/archive/blogs/windowsmobile/custom-soft-input-panel-sip-for-pocket-pc) blog post. The default Microsoft IM (CLSID `{42429667-ae04-11d0-a4f8-00aa00a749b9}`) is registered the same way the docs prescribe for third parties. I got burned twice guessing -- REG_SZ vs REG_DWORD, then `Shell\Keybd` path vs `HKCR\CLSID\*`.

**Correct layout (mirror under HKCR and HKLM\Software\Classes both):**
```
HKCR\CLSID\{guid}
    @  : REG_SZ : "WMRime"                      (display name)

HKCR\CLSID\{guid}\InprocServer32
    @  : REG_SZ : "<full DLL path>"

HKCR\CLSID\{guid}\IsSIPInputMethod              <-- SUBKEY not value!
    @  : REG_SZ : "1"                           <-- default value, STRING not DWORD

HKCR\CLSID\{guid}\DefaultIcon (optional but nice)
    @  : REG_SZ : "<DLL path>,0"
```

**Do NOT set ThreadingModel under InprocServer32.** Comments on the Perryman blog post (CE5 user, May 2007) report that adding `ThreadingModel` actually breaks SIP loading. Microsoft's official IM example omits it.

**Common wrong layouts that don't work:**
- `HKLM\Software\Microsoft\Shell\Keybd\{guid}\IsSIPInputMethod = REG_DWORD 1` -- entirely made up, no MS doc references this path. The shell does not enumerate this hive.
- `HKCR\CLSID\{guid}` with `IsSIPInputMethod` as a REG_DWORD VALUE (not subkey) -- silently ignored.
- `HKCR\CLSID\{guid}\IsSIPInputMethod` as a SUBKEY but with REG_DWORD default -- possibly ignored too (untested; just use REG_SZ "1" like MS docs say).

**How to apply:** When registering any IInputMethod / IInputMethod2 COM server on WinCE/WM, write the `IsSIPInputMethod` SUBKEY with default REG_SZ `"1"`, mirror under both HKCR and HKLM\Software\Classes (HKCR on WinCE is an alias for the latter, but be defensive). Skip ThreadingModel. The SIP appears in the picker without a reboot -- just open Settings > Personal > Input.
