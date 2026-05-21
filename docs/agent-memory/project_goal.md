---
name: Project goal — Rime IME port to Windows Mobile 6.5
description: Overarching goal, target platform, hard constraints and two-DLL architecture for the rime-wm6 port
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
Porting RIME (Rime Input Method Engine, upstream at src/librime/) to Windows Mobile 6.5 / Windows CE 6.0.

**Why:** User wants to use Rime on legacy WM 6.5 hardware (ARMV4I/ARMV5 phones). No other modern IME engine targets this platform.

**How to apply:**

Target platform:
- Windows Mobile 6.5 Professional / WinCE 6.0
- CPU: ARMV4I (project default), but actually compiles to ARMV5
- Toolchain: Visual Studio 2008 (MSVC 9.0), C++03 only
- SDK: Windows Mobile 6 Professional SDK

Hard constraints:
- NO C++11+: no auto, nullptr, lambdas, range-for, template aliases, variadic templates, rvalue references, std::shared_ptr/unique_ptr, std::function, std::filesystem, <chrono>, <thread>, <mutex>, <atomic>, <regex>, <random>
- NO POSIX (no pthread, no mmap, no fork)
- WinCE Win32 API only exposes wide (W) versions; no ANSI (A) versions of most APIs
- Limited stack (~64KB), limited RAM (~32MB user space typical)
- No glog, no Boost (or only the most ancient header-only pieces)

Two-DLL architecture:
- **RimeCore.dll** (rime-wm6/RimeCore/) — engine: ported librime + wince_compat layer, exposes pure C API
- **WMRimeSIP.dll** (rime-wm6/WMRimeSIP/) — frontend: IInputMethod / IInputMethod2 COM shell, soft keyboard rendering, hard keyboard hook (SetWindowsHookEx WH_KEYBOARD_LL), calls RimeCore via the C API

Soft keyboard supports 14/17/18-key non-standard layouts (T9-style coordinate-to-key mapping).
