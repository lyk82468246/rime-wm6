//
// wmrime_sip/log.cc -- file logger impl.
//
#include "log.h"

#include <stdio.h>
#include <string.h>

namespace wmrime {

namespace {

// Try these paths in order. Whichever first CreateFile succeeds wins
// and is cached for the rest of the process lifetime.
//   * \Temp\ -- writable by virtually every WM6 code path
//   * \Windows\ -- we know this is writable since the CAB drops
//                  RimeCore.dll here; useful belt-and-braces
//   * %InstallDir% -- co-located with the install when possible
//   * \Storage Card\ -- last resort if user installed there
//   * \ (root) -- some carrier-locked devices map the user volume
//                  here and reject every other root-like path
const wchar_t* kLogPaths[] = {
  L"\\Temp\\wmrime.log",
  L"\\Windows\\wmrime.log",
  L"\\Program Files\\WMRime\\wmrime.log",
  L"\\Storage Card\\wmrime.log",
  L"\\wmrime.log",
};
const int kLogPathCount = sizeof(kLogPaths) / sizeof(kLogPaths[0]);

// Cached path index that worked last time. -1 = haven't tried; -2 =
// all failed (don't bother retrying).
int g_chosen_path = -1;

// Try a single path. Returns true if write succeeded.
bool TryWritePath(const wchar_t* path, const char* bytes, DWORD len) {
  HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  SetFilePointer(h, 0, NULL, FILE_END);
  DWORD written = 0;
  BOOL ok = WriteFile(h, bytes, len, &written, NULL);
  CloseHandle(h);
  return ok != 0;
}

void WriteRaw(const char* bytes, DWORD len) {
  // Fast path: re-use the path that worked last time.
  if (g_chosen_path >= 0 && g_chosen_path < kLogPathCount) {
    if (TryWritePath(kLogPaths[g_chosen_path], bytes, len)) return;
    // The previously-working path failed; fall through and rescan.
  }
  if (g_chosen_path == -2) return;  // all paths previously failed
  for (int i = 0; i < kLogPathCount; ++i) {
    if (TryWritePath(kLogPaths[i], bytes, len)) {
      g_chosen_path = i;
      return;
    }
  }
  g_chosen_path = -2;
}

void FormatPrefix(char* buf, size_t buflen) {
  DWORD t = GetTickCount();
  _snprintf(buf, buflen, "[%010lu] ", static_cast<unsigned long>(t));
  buf[buflen - 1] = '\0';
}

}  // namespace

void LogLine(const char* msg) {
  if (!msg) return;
  char buf[512];
  FormatPrefix(buf, sizeof(buf));
  size_t pre_len = strlen(buf);
  size_t msg_len = strlen(msg);
  if (pre_len + msg_len + 3 > sizeof(buf)) {
    msg_len = sizeof(buf) - pre_len - 3;
  }
  memcpy(buf + pre_len, msg, msg_len);
  buf[pre_len + msg_len + 0] = '\r';
  buf[pre_len + msg_len + 1] = '\n';
  WriteRaw(buf, static_cast<DWORD>(pre_len + msg_len + 2));
}

void LogLineInt(const char* prefix, int value) {
  char line[256];
  _snprintf(line, sizeof(line), "%s%d", prefix ? prefix : "", value);
  line[sizeof(line) - 1] = '\0';
  LogLine(line);
}

void LogLineHex(const char* prefix, unsigned int value) {
  char line[256];
  _snprintf(line, sizeof(line), "%s0x%08X", prefix ? prefix : "", value);
  line[sizeof(line) - 1] = '\0';
  LogLine(line);
}

void LogLinePtr(const char* prefix, const void* value) {
  char line[256];
  _snprintf(line, sizeof(line), "%s%p", prefix ? prefix : "", value);
  line[sizeof(line) - 1] = '\0';
  LogLine(line);
}

void LogLineW(const char* prefix, const wchar_t* value) {
  // Naive UTF-16 -> ASCII (high bytes truncated). Good enough for
  // logging paths whose ASCII portion is the useful part.
  char wbuf[256] = {0};
  if (value) {
    int i = 0;
    while (value[i] && i < static_cast<int>(sizeof(wbuf) - 1)) {
      wbuf[i] = static_cast<char>(value[i] & 0xff);
      ++i;
    }
    wbuf[i] = '\0';
  }
  char line[512];
  _snprintf(line, sizeof(line), "%s%s", prefix ? prefix : "", wbuf);
  line[sizeof(line) - 1] = '\0';
  LogLine(line);
}

}  // namespace wmrime
