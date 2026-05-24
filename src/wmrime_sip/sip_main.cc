//
// wmrime_sip/sip_main.cc -- COM DLL entry points + global state for
// WMRimeSIP. NO DllMain (per the project's WinCE/MSVC9 finding -- see
// feedback_dllmain_msvc9.md in agent memory). Initialization happens
// lazily inside EnsureRimeInitialized; shutdown rides on a static
// dtor.
//
// Exports:
//   * DllGetClassObject       -- COM's class lookup hook
//   * DllCanUnloadNow         -- COM's unload gate
//   * DllRegisterServer       -- writes CLSID + SIP-key entries to HKLM
//   * DllUnregisterServer     -- removes them
//
// We deliberately ship the four exports via __declspec(dllexport)
// rather than a .def file to keep the project layout flat. WinCE COM
// supports either; the names are queried by GetProcAddress in the
// system COM activator.
//
#include "sip_globals.h"

#include <windows.h>
// INITGUID makes DEFINE_GUID in subsequently-included headers (clsids.h,
// sip.h) emit each GUID's storage in this TU. Without it CLSID_WMRimeSIP
// and IID_IInputMethod* are unresolved externals at link time.
#define INITGUID
#include <objbase.h>
#include <initguid.h>
#include <sip.h>

#include <new>
#include <string>

#include <rime_api.h>

#include "class_factory.h"
#include "clsids.h"

namespace wmrime {

LONG g_object_count = 0;
LONG g_lock_count = 0;

namespace {

CRITICAL_SECTION g_init_cs;
bool g_init_cs_ready = false;
bool g_rime_initialized = false;

// Make sure the critical section is created exactly once. We can't use
// InitOnceExecuteOnce (XP+) on WinCE; the SipBootstrapper static below
// runs at DLL load via .CRT$XCU and primes everything.
void EnsureInitCs() {
  if (g_init_cs_ready) return;
  InitializeCriticalSection(&g_init_cs);
  g_init_cs_ready = true;
}

// Read shared_data_dir from the registry, falling back to a hardcoded
// path. Devices that installed the .cab will have:
//   HKLM\Software\WMRime\SharedDataDir = "\Program Files\WMRime\data"
std::wstring ReadSharedDataDir() {
  HKEY hKey = NULL;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"Software\\WMRime", 0, 0, &hKey) == ERROR_SUCCESS) {
    wchar_t buf[MAX_PATH];
    DWORD type = 0;
    DWORD cb = sizeof(buf);
    if (RegQueryValueExW(hKey, L"SharedDataDir", NULL, &type,
                         reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS
        && type == REG_SZ) {
      RegCloseKey(hKey);
      return std::wstring(buf);
    }
    RegCloseKey(hKey);
  }
  // Fallback: ship the data folder alongside the DLL during dev.
  return std::wstring(L"\\Program Files\\WMRime\\data");
}

// Trampoline: keep a UTF-8 narrow copy alive past the call.
struct TraitsHolder {
  std::string shared;
  std::string user;
  std::string app;
};

// One-shot bootstrapper + reaper. Constructed at DLL load via the
// .CRT$XCU section (MSVC's standard static initialization point), and
// destroyed at unload.
class SipBootstrapper {
 public:
  SipBootstrapper() {
    EnsureInitCs();
  }
  ~SipBootstrapper() {
    if (g_rime_initialized) {
      RimeFinalize();
      g_rime_initialized = false;
    }
    if (g_init_cs_ready) {
      DeleteCriticalSection(&g_init_cs);
      g_init_cs_ready = false;
    }
  }
};
SipBootstrapper g_bootstrapper;

}  // namespace

HINSTANCE GetSipModule() {
  return GetModuleHandleW(L"WMRimeSIP.dll");
}

void EnsureRimeInitialized() {
  EnsureInitCs();
  EnterCriticalSection(&g_init_cs);
  if (!g_rime_initialized) {
    std::wstring shared = ReadSharedDataDir();
    // Convert to UTF-8 narrow for rime_api.
    int n = WideCharToMultiByte(CP_UTF8, 0, shared.c_str(), -1,
                                NULL, 0, NULL, NULL);
    static TraitsHolder holder;
    if (n > 0) {
      holder.shared.assign(static_cast<size_t>(n - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, shared.c_str(), -1,
                          &holder.shared[0], n, NULL, NULL);
    }
    holder.app = "rime.wm6";
    RimeTraits traits;
    ZeroMemory(&traits, sizeof(traits));
    traits.data_size = static_cast<int>(sizeof(traits) - sizeof(int));
    traits.shared_data_dir = holder.shared.c_str();
    traits.user_data_dir = NULL;
    traits.app_name = holder.app.c_str();
    RimeInitialize(&traits);
    g_rime_initialized = true;
  }
  LeaveCriticalSection(&g_init_cs);
}

}  // namespace wmrime

// ======================================================================
// DLL exports
// ======================================================================
// Function signatures must exactly match those declared in objbase.h on
// the WM6 SDK -- redeclaring with __declspec(dllexport) would conflict.
// Exports come from WMRimeSIP.def instead.

extern "C" HRESULT STDAPICALLTYPE
DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (!IsEqualCLSID(rclsid, CLSID_WMRimeSIP)) return CLASS_E_CLASSNOTAVAILABLE;
  wmrime::RimeClassFactory* factory = new (std::nothrow) wmrime::RimeClassFactory();
  if (!factory) return E_OUTOFMEMORY;
  HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  return hr;
}

extern "C" HRESULT STDAPICALLTYPE
DllCanUnloadNow(void) {
  return (wmrime::g_object_count == 0 && wmrime::g_lock_count == 0)
      ? S_OK : S_FALSE;
}

// Helper: open or create a registry key, write a string value.
static LONG WriteRegStringW(HKEY root, const wchar_t* subkey,
                            const wchar_t* name, const wchar_t* value) {
  HKEY hKey = NULL;
  DWORD disp = 0;
  LONG err = RegCreateKeyExW(root, subkey, 0, NULL, 0, 0, NULL, &hKey, &disp);
  if (err != ERROR_SUCCESS) return err;
  err = RegSetValueExW(hKey, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value),
                       static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
  RegCloseKey(hKey);
  return err;
}

// Helper: write a DWORD value to an existing or newly-created key.
static LONG WriteRegDwordW(HKEY root, const wchar_t* subkey,
                           const wchar_t* name, DWORD value) {
  HKEY hKey = NULL;
  DWORD disp = 0;
  LONG err = RegCreateKeyExW(root, subkey, 0, NULL, 0, 0, NULL, &hKey, &disp);
  if (err != ERROR_SUCCESS) return err;
  err = RegSetValueExW(hKey, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(hKey);
  return err;
}

// Format the CLSID as "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" in UTF-16.
static void FormatClsid(REFCLSID clsid, wchar_t* buf, size_t n) {
  StringFromGUID2(clsid, buf, static_cast<int>(n));
}

extern "C" HRESULT STDAPICALLTYPE
DllRegisterServer(void) {
  wchar_t clsid_str[64];
  FormatClsid(CLSID_WMRimeSIP, clsid_str, 64);

  wchar_t module_path[MAX_PATH];
  GetModuleFileNameW(wmrime::GetSipModule(), module_path, MAX_PATH);

  // HKCR\CLSID\{ours}\InprocServer32 = module path
  // (On WinCE HKCR is an alias; we use HKLM\Software\Classes for clarity.)
  wchar_t key[256];
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s", clsid_str);
  LONG err = WriteRegStringW(HKEY_LOCAL_MACHINE, key, NULL, L"WMRime SIP");
  if (err != ERROR_SUCCESS) return E_FAIL;

  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid_str);
  err = WriteRegStringW(HKEY_LOCAL_MACHINE, key, NULL, module_path);
  if (err != ERROR_SUCCESS) return E_FAIL;
  err = WriteRegStringW(HKEY_LOCAL_MACHINE, key, L"ThreadingModel", L"Apartment");
  if (err != ERROR_SUCCESS) return E_FAIL;

  // Some WinCE COM consumers query HKCR\CLSID directly rather than
  // HKLM\Software\Classes\CLSID, so mirror the registration under both.
  wsprintfW(key, L"CLSID\\%s", clsid_str);
  WriteRegStringW(HKEY_CLASSES_ROOT, key, NULL, L"WMRime SIP");
  wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid_str);
  WriteRegStringW(HKEY_CLASSES_ROOT, key, NULL, module_path);
  WriteRegStringW(HKEY_CLASSES_ROOT, key, L"ThreadingModel", L"Apartment");

  // Register as an SIP under HKLM\Software\Microsoft\Shell\Keybd\{ours}.
  // The shell SIP picker enumerates this subkey and filters entries
  // whose IsSIPInputMethod == REG_DWORD 1. A REG_SZ "1" is silently
  // rejected -- caught us once already.
  wsprintfW(key, L"Software\\Microsoft\\Shell\\Keybd\\%s", clsid_str);
  err = WriteRegStringW(HKEY_LOCAL_MACHINE, key, L"Name", L"WMRime");
  if (err != ERROR_SUCCESS) return E_FAIL;
  err = WriteRegDwordW(HKEY_LOCAL_MACHINE, key, L"IsSIPInputMethod", 1);
  if (err != ERROR_SUCCESS) return E_FAIL;
  WriteRegDwordW(HKEY_LOCAL_MACHINE, key, L"PreferredImage", 0);

  return S_OK;
}

extern "C" HRESULT STDAPICALLTYPE
DllUnregisterServer(void) {
  wchar_t clsid_str[64];
  FormatClsid(CLSID_WMRimeSIP, clsid_str, 64);
  wchar_t key[256];

  wsprintfW(key, L"Software\\Microsoft\\Shell\\Keybd\\%s", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

  wsprintfW(key, L"Software\\Classes\\CLSID\\%s", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

  // Mirror under HKCR (best-effort; ignore errors if not present).
  wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid_str);
  RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
  wsprintfW(key, L"CLSID\\%s", clsid_str);
  RegDeleteKeyW(HKEY_CLASSES_ROOT, key);

  return S_OK;
}
