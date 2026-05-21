---
name: RimeCore vcproj must enable RTTI
description: WinCE VS2008 projects default to /GR-; dynamic_pointer_cast breaks silently without RuntimeTypeInfo=true
type: project
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
Both Debug and Release configurations of `rime-wm6/RimeCore/RimeCore.vcproj` MUST set `RuntimeTypeInfo="true"` on `VCCLCompilerTool`. Same will apply to WMRimeSIP.vcproj once it grows code.

**Why:** Windows Mobile 6 Professional SDK VS2008 templates default RTTI to OFF (`/GR-`). Rime's component dispatch is built on `As<X>(component)` / `Is<X>(component)` which expand to `wince::dynamic_pointer_cast` -> `dynamic_cast`. Without RTTI, `dynamic_cast` on a polymorphic type emits warning C4541 and at runtime returns unpredictable garbage. Detected 2026-05 when the smoke test in dllmain.cc exercised `As<Cat>(an<AnimalBase>)`. Symptom: every filter / translator / processor lookup that uses As/Is silently breaks.

**How to apply:**

- When adding new configurations or new vcproj files for this project, set `RuntimeTypeInfo="true"` from the start.
- Cost is a few extra bytes of typeinfo metadata per polymorphic class -- negligible for an IME (~20 polymorphic types in the rime engine).
- Do NOT try to work around it by writing manual type-tag dispatch -- rime's architecture is fundamentally RTTI-dependent in its current shape.
