//
// wmrime_sip/clsids.h -- CLSID + IID definitions for WMRimeSIP.
//
// SIP DLLs on Windows Mobile / WinCE are COM in-proc servers. The OS
// finds them by looking up our CLSID under
//   HKLM\Software\Microsoft\Shell\Keybd\<CLSID>
// and treating us as an implementer of IInputMethod (or IInputMethod2).
//
// CLSID_WMRimeSIP was generated fresh for this project. Don't reuse
// across forks -- end-users will get registration conflicts.
//
#ifndef WMRIME_SIP_CLSIDS_H_
#define WMRIME_SIP_CLSIDS_H_

#include <objbase.h>

// {7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47}
// Generated 2026 for the WinCE Rime port. Do not change once shipped --
// users' SIP selection in Windows settings is keyed off this CLSID.
DEFINE_GUID(CLSID_WMRimeSIP,
            0x7B9F6D8E, 0x4A2C, 0x4F1E,
            0x9D, 0x6B, 0x3E, 0x5A, 0x8C, 0x2D, 0x1F, 0x47);

#endif  // WMRIME_SIP_CLSIDS_H_
