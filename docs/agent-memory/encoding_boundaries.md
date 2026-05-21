---
name: Encoding boundaries — UTF-8 inside, UTF-16 at edges
description: Rule for where UTF-8 ↔ UTF-16 conversions happen in the two-DLL architecture
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
librime uses `std::string` = UTF-8 throughout. WinCE Win32/COM uses `wchar_t` = UTF-16. Conversions are confined to one place.

**Why:** WinCE has almost no ANSI (A) Win32 APIs — only wide (W) versions. So the moment we touch a system call or COM interface, we must be in UTF-16. But the upstream librime code (and all .yaml/.bin data files) are UTF-8. Keeping the conversion at one boundary avoids encoding bugs and makes the rest of the port a mechanical text-replace exercise.

**How to apply:**

- Inside `RimeCore.dll` (the engine):
  - Everything is `char*` / `std::string` UTF-8
  - File paths are UTF-8 strings inside the engine, converted to UTF-16 only when calling CreateFileW etc.
  - Conversion utilities live in `src/wince_compat/utf.h`:
    - `std::wstring utf8_to_utf16(const std::string&)`
    - `std::string utf16_to_utf8(const std::wstring&)`
    - thin wrappers over `MultiByteToWideChar(CP_UTF8, ...)` / `WideCharToMultiByte(CP_UTF8, ...)`
- Public C API of RimeCore.dll: all `const char*` are UTF-8 (matches upstream rime_api.h convention)
- Inside `WMRimeSIP.dll` (the shell):
  - Talks to Windows in UTF-16 (LPCWSTR, BSTR, etc.)
  - Converts to UTF-8 immediately before any call into RimeCore.dll
  - Converts UTF-8 results back to UTF-16 before returning to Windows
