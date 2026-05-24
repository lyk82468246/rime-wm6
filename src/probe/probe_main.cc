//
// probe/probe_main.cc -- standalone diagnostic for WMRimeSIP.dll
//
// Usage: install via the WMRime CAB or copy the EXE to the device,
// double-tap it. A MessageBox shows whether WMRimeSIP.dll can be
// loaded into a normal-process address space and, if not, the Win32
// last-error code.
//
// Common error codes seen here:
//   126 (ERROR_MOD_NOT_FOUND)   -- a dependent DLL (RimeCore.dll, etc.)
//                                  isn't on the loader's search path.
//   127 (ERROR_PROC_NOT_FOUND)  -- an imported export is missing.
//   193 (ERROR_BAD_EXE_FORMAT)  -- ARMV4I vs device CPU mismatch.
//     5 (ERROR_ACCESS_DENIED)   -- security policy refused the load.
//     2 (ERROR_FILE_NOT_FOUND)  -- WMRimeSIP.dll itself isn't where we
//                                  think it is.
//
// The EXE is intentionally trivial: no CRT-string work, no localized
// formatting -- just a couple of wsprintfW calls and a MessageBox.
//
#include <windows.h>

static const wchar_t* kSipPath = L"\\Program Files\\WMRime\\WMRimeSIP.dll";
static const wchar_t* kCorePath = L"\\Windows\\RimeCore.dll";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  wchar_t msg[1024];

  // 1) Probe RimeCore.dll directly first -- if it can't load, the SIP
  //    can't either.
  HMODULE hCore = LoadLibraryW(kCorePath);
  DWORD coreErr = hCore ? 0 : GetLastError();

  // 2) Probe WMRimeSIP.dll. This pulls RimeCore as a static dependency
  //    so the loader exercises the search path the shell uses.
  HMODULE hSip = LoadLibraryW(kSipPath);
  DWORD sipErr = hSip ? 0 : GetLastError();

  // 3) If SIP loaded, see if DllGetClassObject is reachable -- proves
  //    CoCreateInstance would also work.
  FARPROC pDgco = NULL;
  DWORD dgcoErr = 0;
  if (hSip) {
    pDgco = GetProcAddress(hSip, L"DllGetClassObject");
    if (!pDgco) dgcoErr = GetLastError();
  }

  wsprintfW(msg,
            L"RimeCore.dll  -> %s\r\n"
            L"  hModule=0x%08X  err=%lu\r\n"
            L"\r\n"
            L"WMRimeSIP.dll -> %s\r\n"
            L"  hModule=0x%08X  err=%lu\r\n"
            L"\r\n"
            L"DllGetClassObject in WMRimeSIP:\r\n"
            L"  proc=0x%08X  err=%lu",
            hCore ? L"OK" : L"FAIL",
            (unsigned)hCore, (unsigned long)coreErr,
            hSip ? L"OK" : L"FAIL",
            (unsigned)hSip, (unsigned long)sipErr,
            (unsigned)pDgco, (unsigned long)dgcoErr);

  MessageBoxW(NULL, msg, L"WMRime Probe", MB_OK | MB_ICONINFORMATION);

  if (hSip) FreeLibrary(hSip);
  if (hCore) FreeLibrary(hCore);
  return 0;
}
