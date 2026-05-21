//
// wince_compat/mutex.h -- minimal std::mutex / lock_guard replacement.
//
// Wraps Win32 CRITICAL_SECTION (available on WinCE since 3.0). Even though
// the engine has RIME_NO_THREADING set, the IME shell (WMRimeSIP) may invoke
// RimeCore from the WH_KEYBOARD_LL hook thread in parallel with the UI
// thread. Long-term we will guard the session map with one of these.
//
// Non-recursive by default. For recursive locks, WinCE CRITICAL_SECTION is
// actually recursive by spec, but std::mutex semantically isn't, so callers
// should not rely on that.
//
#ifndef WINCE_COMPAT_MUTEX_H_
#define WINCE_COMPAT_MUTEX_H_

#include <windows.h>

namespace wince {

class mutex {
 public:
  mutex() { InitializeCriticalSection(&cs_); }
  ~mutex() { DeleteCriticalSection(&cs_); }

  void lock()   { EnterCriticalSection(&cs_); }
  void unlock() { LeaveCriticalSection(&cs_); }

 private:
  mutex(const mutex&);             // not copyable
  mutex& operator=(const mutex&);  // not assignable
  CRITICAL_SECTION cs_;
};

class lock_guard {
 public:
  explicit lock_guard(mutex& m) : m_(m) { m_.lock(); }
  ~lock_guard() { m_.unlock(); }

 private:
  lock_guard(const lock_guard&);
  lock_guard& operator=(const lock_guard&);
  mutex& m_;
};

}  // namespace wince

#endif  // WINCE_COMPAT_MUTEX_H_
