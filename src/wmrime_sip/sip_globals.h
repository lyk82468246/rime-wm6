//
// wmrime_sip/sip_globals.h -- DLL-wide state shared across all the
// COM objects. Lives in its own TU so DllCanUnloadNow / class_factory /
// rime_input_method all see the same counters.
//
#ifndef WMRIME_SIP_SIP_GLOBALS_H_
#define WMRIME_SIP_SIP_GLOBALS_H_

#include <windows.h>

namespace wmrime {

// Atomic counts. Bumped by RIME-IM-object ctor/dtor and by the class
// factory's LockServer(TRUE/FALSE).
extern LONG g_object_count;
extern LONG g_lock_count;

// HINSTANCE of WMRimeSIP.dll. Lazily resolved on first access via
// GetModuleHandleW(L"WMRimeSIP.dll"). Used for registering window
// classes whose WNDPROC lives in this DLL.
HINSTANCE GetSipModule();

// One-time RimeCore initialization. Idempotent and thread-safe via
// CRITICAL_SECTION. Called from RimeInputMethod::Select on first
// activation; tears down via RimeFinalize from the static
// SipShutdown object in sip_main.cc.
void EnsureRimeInitialized();

}  // namespace wmrime

#endif  // WMRIME_SIP_SIP_GLOBALS_H_
