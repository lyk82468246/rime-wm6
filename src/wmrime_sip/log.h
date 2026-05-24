//
// wmrime_sip/log.h -- ultra-cheap file logger for on-device diagnostics.
//
// Goal: when the SIP fails to load or behave on the device, we can
// pull a step-by-step trace via ActiveSync rather than guess.
//
// Writes to:
//   \Program Files\WMRime\wmrime.log
//
// Each entry is one line: "[<GetTickCount>] <message>\r\n".
//
// Safe to call from anywhere -- failures are silent (no exceptions,
// no errors propagated). Reentrancy is fine because we open + close
// the file each time; the OS serializes per-handle access.
//
// To disable in production, set WMRIME_LOG_ENABLE to 0 in log.cc.
//
#ifndef WMRIME_SIP_LOG_H_
#define WMRIME_SIP_LOG_H_

#include <windows.h>

namespace wmrime {

// Append one line. `msg` is ASCII; the function copies it.
void LogLine(const char* msg);

// Variants that format a single value.
void LogLineInt(const char* prefix, int value);
void LogLineHex(const char* prefix, unsigned int value);
void LogLinePtr(const char* prefix, const void* value);
void LogLineW(const char* prefix, const wchar_t* value);

}  // namespace wmrime

#endif  // WMRIME_SIP_LOG_H_
