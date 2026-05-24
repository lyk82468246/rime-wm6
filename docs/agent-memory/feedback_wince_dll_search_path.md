---
name: WinCE DLL search excludes the loaded module's directory
description: Why a SIP DLL in \Program Files\App\ can't find its sibling DLL there, and the deploy-time fix
type: feedback
---
On Windows CE 6 (and WM6), the loader's search path for **dependent** DLLs is roughly:
1. ROM modules
2. The loading process's working directory
3. `\Windows\`

**It does NOT include the directory of the DLL that's importing**, unlike desktop Windows. This bites hard when you ship a multi-DLL app (e.g. an SIP plus an engine):

- shell loads `\Program Files\App\AppSIP.dll` via the registry's full path → fine
- AppSIP.dll has a static import to `Engine.dll` (in the same directory)
- loader searches `{shell_cwd, \Windows\}` for Engine.dll → not there
- AppSIP.dll fails to load → `CoCreateInstance` returns CLASS_E_CLASSNOTAVAILABLE
- shell's SIP picker silently drops the entry (no error message)

**Symptom**: SIP entry exists in `HKLM\Software\Microsoft\Shell\Keybd\<CLSID>` with all the right fields (Name REG_SZ, IsSIPInputMethod REG_DWORD 1), CLSID is registered, files are in place, but the SIP doesn't appear in Settings > Input or the keyboard chevron picker.

**Fix in INF**: put the dependent (engine) DLL in `\Windows\` (= `%CE2%`), the SIP DLL stays in the app dir.

```
[DestinationDirs]
Files.Sip       = 0, %InstallDir%       ; \Program Files\App\
Files.Engine    = 0, %CE2%              ; \Windows\

[Files.Sip]
AppSIP.dll

[Files.Engine]
Engine.dll
```

**Alternatives** if you don't want to pollute `\Windows\`:
- Statically merge the two DLLs into one (clean but build refactor).
- Use `/DELAYLOAD:Engine.dll` + a static initializer in AppSIP.dll that calls `LoadLibraryW(full_path_to_engine)` before the first delay-loaded call. Loader will use the cached handle.

**Why `\Windows\` and not bundle into one DLL by default:** the merge route is cleaner but means a hot path in the SIP DLL covers all of librime; harder to share with future test harness EXEs. Splitting + `\Windows\` keeps RimeCore.dll reusable across multiple consumers (SIP, setup.exe, test apps).

**How to apply:** When shipping any WinCE/WM in-proc COM server that imports from a sibling DLL, the sibling MUST be installed somewhere on the loader's search path. `\Windows\` is the boring-correct answer.
