---
name: MSVC9 vc80.pdb lock recovery
description: When the WinCE build emits C2471/C1083 against vc80.pdb, delete the file and rebuild — not a code bug
type: feedback
originSessionId: 3ffd7d86-f40d-4114-9d5d-4a911b8d6d89
---
If `./build.bat` fails with the pair:
  * `error C2471: cannot update program database vc80.pdb`
  * `fatal error C1083: cannot open program database file vc80.pdb`

the cause is almost always a stale read/write lock on the .pdb file, not a code-level issue. Background: MSVC 9.0 funnels debug-info writes through a single `vc80.pdb` per output dir; parallel `cl` invocations contend for it via `mspdbsrv.exe`, and IDE/AV/explorer-thumbnail handles can keep the file locked after the previous build aborted.

**How to apply:** when you see this exact error, do NOT chase a phantom code bug. Just:

```
rm -f "rime-wm6/RimeCore/Windows Mobile 6 Professional SDK (ARMV4I)/Debug/vc80.pdb" \
       "rime-wm6/RimeCore/Windows Mobile 6 Professional SDK (ARMV4I)/Debug/vc90.pdb"
./build.bat
```

The second build will recreate the PDB from scratch. If it recurs every build, suspect a stuck `mspdbsrv.exe` (kill it via Task Manager) or AV scanning the output directory.

**Why:** Hit this on 2026-05-18 after editing schema.h to fix a real C2664 error; the rebuild then failed with C2471 on vc80.pdb across multiple .cc files, looking superficially like the schema change broke unrelated translation units. It wasn't; the actual code change was correct, only the PDB was wedged.

Do NOT confuse with C1083 for missing source headers (those have a real filename, like "cannot open include file: stdint.h"). The PDB-lock variant always names vc80.pdb / vc90.pdb specifically.
