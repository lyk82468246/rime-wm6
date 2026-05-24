//
// dllmain.cc -- intentionally minimal.
//
// This file used to host a giant smoke test ("SmokeTestRunner") that
// ran ~40 module exercises at DLL_PROCESS_ATTACH. The smoke test was a
// development-time scaffolding: it forced template instantiation of the
// wince_compat shims so MSVC9 would emit any C++03 backport diagnostics
// at build time rather than at link time of downstream consumers.
//
// That scaffolding turned out to be poison in production: any of the 40
// exercises blowing up on real WM6 hardware kills the entire DLL load
// (the CRT's static-init loop returns FALSE, LoadLibrary then returns
// ERROR_DLL_INIT_FAILED = 1114). Symptom: WMRime appears in the SIP
// picker (registry-only enumeration) but selecting it silently reverts
// to the default keyboard because CoCreateInstance can't load us.
//
// The file is kept in the build for two practical reasons:
//   * Some part of the MSVC9 link toolchain on WinCE expects at least
//     one .cc with a CRT-recognised entry point in each DLL. Keeping
//     a stub here avoids gambling on whether removing it causes a
//     mysterious linker quirk.
//   * Anchors the project file's "Source Files" filter so VS doesn't
//     show an empty subtree.
//
// We do NOT define a custom DllMain -- the WinCE C runtime supplies
// _DllMainCRTStartup which runs all static initialisers on attach and
// returns TRUE for the other notification reasons. Custom DllMain on
// MSVC9 + WM6 Pro SDK triggers a spurious C2731 "cannot overload
// function" diagnostic (see feedback_dllmain_msvc9.md in agent memory).
//
// To resurrect the smoke runner for a focused debug build:
//   * git show f402e9d:rime-wm6/RimeCore/dllmain.cc  (or any earlier
//     commit on main) has the full SmokeTestRunner source. Drop it
//     into a separate .cc gated behind #ifdef RIMECORE_SMOKE_TEST and
//     add the define to a Debug-Smoke configuration. Do NOT add it to
//     Release.
//

#include <windows.h>

// Touching one Win32 symbol guarantees coredll.dll appears in the
// import table for this TU, which keeps the linker happy on /MT
// builds where dead-code elimination is aggressive.
extern "C" __declspec(dllexport) DWORD RimeCoreModuleAnchor(void) {
  return GetTickCount();
}
