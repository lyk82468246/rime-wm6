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
#include "log.h"

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
    LogLine("=== WMRimeSIP DLL load (SipBootstrapper ctor) ===");
    // Stamp the build identity into the log so we can verify, by
    // inspecting the log itself, that the device is running the DLL
    // we think it is (not a stale cached copy from a previous CAB).
    // __DATE__/__TIME__ are baked in at compile time.
    LogLine("BUILD: " __DATE__ " " __TIME__ " feature=create-child-window");
  }
  ~SipBootstrapper() {
    LogLine("=== WMRimeSIP DLL unload (SipBootstrapper dtor) ===");
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
    LogLine("EnsureRimeInitialized: starting");
    std::wstring shared = ReadSharedDataDir();
    LogLineW("EnsureRimeInitialized: shared_data_dir = ", shared.c_str());
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
    LogLine("EnsureRimeInitialized: calling RimeInitialize");
    RimeInitialize(&traits);
    LogLine("EnsureRimeInitialized: RimeInitialize returned");
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
  wmrime::LogLine("DllGetClassObject entered");
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (!IsEqualCLSID(rclsid, CLSID_WMRimeSIP)) {
    wmrime::LogLine("DllGetClassObject: rclsid != CLSID_WMRimeSIP, returning CLASS_E_CLASSNOTAVAILABLE");
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  wmrime::RimeClassFactory* factory = new (std::nothrow) wmrime::RimeClassFactory();
  if (!factory) {
    wmrime::LogLine("DllGetClassObject: OOM allocating ClassFactory");
    return E_OUTOFMEMORY;
  }
  HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  wmrime::LogLineHex("DllGetClassObject returning hr=", static_cast<unsigned int>(hr));
  return hr;
}

extern "C" HRESULT STDAPICALLTYPE
DllCanUnloadNow(void) {
  return (wmrime::g_object_count == 0 && wmrime::g_lock_count == 0)
      ? S_OK : S_FALSE;
}

// Open-or-create a registry key and write a REG_SZ value.
// When `name` is NULL, the default value is written.
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

// Format the CLSID as "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" in UTF-16.
static void FormatClsid(REFCLSID clsid, wchar_t* buf, size_t n) {
  StringFromGUID2(clsid, buf, static_cast<int>(n));
}

// Authoritative WinCE / WM SIP registration layout (per the WinCE 5.0
// docs at learn.microsoft.com/en-us/previous-versions/windows/embedded/aa452674
// and Marcus Perryman's official Custom-SIP-for-Pocket-PC blog post):
//
//   HKCR\CLSID\{ours}                       @ = REG_SZ "WMRime"
//   HKCR\CLSID\{ours}\InprocServer32        @ = REG_SZ "<DLL path>"
//   HKCR\CLSID\{ours}\IsSIPInputMethod      @ = REG_SZ "1"     <-- subkey, not value
//   HKCR\CLSID\{ours}\DefaultIcon           @ = REG_SZ "<DLL>,0"
//
// Wrong attempts that have already burned us:
//   * HKLM\Software\Microsoft\Shell\Keybd\{ours}\... -- this path does
//     not appear in any Microsoft doc; the SIP picker ignores it.
//   * IsSIPInputMethod as a REG_DWORD value on the parent key -- only
//     the SUBKEY form (with a string default) is recognized.
//   * Writing ThreadingModel under InprocServer32 -- reported to break
//     SIP loading on CE5; the official MS example omits it.
//
// HKCR on WinCE is `HKLM\Software\Classes`. We mirror under both to be
// defensive in case a future ROM consults only one of the two hives.
extern "C" HRESULT STDAPICALLTYPE
DllRegisterServer(void) {
  wchar_t clsid_str[64];
  FormatClsid(CLSID_WMRimeSIP, clsid_str, 64);

  wchar_t module_path[MAX_PATH];
  GetModuleFileNameW(wmrime::GetSipModule(), module_path, MAX_PATH);

  wchar_t key[256];
  LONG err;

  // ---- HKCR side ----
  wsprintfW(key, L"CLSID\\%s", clsid_str);
  err = WriteRegStringW(HKEY_CLASSES_ROOT, key, NULL, L"WMRime");
  if (err != ERROR_SUCCESS) return E_FAIL;

  wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid_str);
  err = WriteRegStringW(HKEY_CLASSES_ROOT, key, NULL, module_path);
  if (err != ERROR_SUCCESS) return E_FAIL;

  wsprintfW(key, L"CLSID\\%s\\IsSIPInputMethod", clsid_str);
  err = WriteRegStringW(HKEY_CLASSES_ROOT, key, NULL, L"1");
  if (err != ERROR_SUCCESS) return E_FAIL;

  wchar_t icon_value[MAX_PATH + 4];
  wsprintfW(icon_value, L"%s,0", module_path);
  wsprintfW(key, L"CLSID\\%s\\DefaultIcon", clsid_str);
  WriteRegStringW(HKEY_CLASSES_ROOT, key, NULL, icon_value);

  // ---- HKLM\Software\Classes mirror ----
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s", clsid_str);
  WriteRegStringW(HKEY_LOCAL_MACHINE, key, NULL, L"WMRime");
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid_str);
  WriteRegStringW(HKEY_LOCAL_MACHINE, key, NULL, module_path);
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\IsSIPInputMethod", clsid_str);
  WriteRegStringW(HKEY_LOCAL_MACHINE, key, NULL, L"1");
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\DefaultIcon", clsid_str);
  WriteRegStringW(HKEY_LOCAL_MACHINE, key, NULL, icon_value);

  return S_OK;
}

extern "C" HRESULT STDAPICALLTYPE
DllUnregisterServer(void) {
  wchar_t clsid_str[64];
  FormatClsid(CLSID_WMRimeSIP, clsid_str, 64);
  wchar_t key[256];

  // Delete leaf subkeys first (RegDeleteKey on WinCE refuses to delete
  // a key that still has children, unlike NT). Best-effort: ignore
  // errors when the key is already absent.
  wsprintfW(key, L"CLSID\\%s\\DefaultIcon", clsid_str);
  RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
  wsprintfW(key, L"CLSID\\%s\\IsSIPInputMethod", clsid_str);
  RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
  wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid_str);
  RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
  wsprintfW(key, L"CLSID\\%s", clsid_str);
  RegDeleteKeyW(HKEY_CLASSES_ROOT, key);

  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\DefaultIcon", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\IsSIPInputMethod", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s", clsid_str);
  RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);

  return S_OK;
}
