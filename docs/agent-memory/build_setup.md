---
name: Build setup — VS2008 solution and projects
description: Solution/project file locations, build invocation, project DLL outputs and current empty state
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
Build entry points and project structure.

**Why:** Two VS2008 project shells exist but are completely empty (no <File> entries). Need to know what config to target and where DLLs land.

**How to apply:**

- Solution: `rime-wm6/rime-wm6.sln` (VS 2008, Format Version 10.00)
- Projects (both .vcproj, both empty source-wise as of 2026-05-17):
  - `rime-wm6/RimeCore/RimeCore.vcproj` → outputs `RimeCore.dll` (defines `RIMECORE_EXPORTS`)
  - `rime-wm6/WMRimeSIP/WMRimeSIP.vcproj` → outputs `WMRimeSIP.dll` (defines `WMRIMESIP_EXPORTS`)
- Both projects: ConfigurationType=2 (DLL), CharacterSet=1 (UNICODE), `/subsystem:windowsce,5.02`
- Platform: `Windows Mobile 6 Professional SDK (ARMV4I)` (the only one configured)
- Configurations: Debug + Release, both ARMV4I
- Runtime library: Debug=Multi-threaded Debug (1), Release=Multi-threaded (0)
- Build command (`build.bat`):
  ```bat
  call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\vcvarsall.bat"
  devenv rime-wm6\rime-wm6.sln /Build "Debug|Windows Mobile 6 Professional SDK (ARMV4I)"
  ```
- Predefined preprocessor symbols set by SDK: `_WIN32_WCE`, `UNDER_CE`, `WINCE`, `_UNICODE`, `UNICODE`, `$(ARCHFAM)`, `$(_ARCHFAM_)`

Note: the upstream `src/librime/deps/` directories (glog, leveldb, marisa-trie, opencc, yaml-cpp, googletest) are all EMPTY — submodules never checked out. This is intentional for the port: we provide replacements, we don't try to compile upstream deps.
