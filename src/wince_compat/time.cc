//
// wince_compat/time.cc -- implementation of time(time_t*) for WinCE.
//
// MSVC 9.0's WinCE 6 SDK declares `time()` in <time.h> but coredll.lib does
// not export it, so the linker emits LNK2019 against any TU that calls
// time(). We provide a definition here that wraps GetTickCount(), making
// the function monotonic seconds-since-boot rather than a real UNIX
// epoch.
//
// This is NOT wall-clock time. Do not use it for anything that crosses
// process boundaries or expects a real UNIX epoch. Acceptable for our
// MVP use case: Session::last_active_time_ for the 5-minute idle reap.
// When yaml-cpp / schema deployer come back and need real timestamps,
// swap this for a SYSTEMTIME-to-time_t conversion.
//
#include <time.h>
#include <windows.h>

extern "C" time_t time(time_t* out) {
  time_t t = (time_t)(GetTickCount() / 1000);
  if (out) *out = t;
  return t;
}
