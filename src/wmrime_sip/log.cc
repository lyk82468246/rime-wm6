//
// wmrime_sip/log.cc -- file logger impl.
//
#include "log.h"

#include <stdio.h>
#include <string.h>

namespace wmrime {

namespace {

const wchar_t* kLogPath = L"\\Program Files\\WMRime\\wmrime.log";

// All paths here use the Win32 API directly (no CRT) so we have a
// reliable channel even before the CRT is fully initialized.
void WriteRaw(const char* bytes, DWORD len) {
  HANDLE h = CreateFileW(kLogPath, GENERIC_WRITE, FILE_SHARE_READ,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  SetFilePointer(h, 0, NULL, FILE_END);
  DWORD written = 0;
  WriteFile(h, bytes, len, &written, NULL);
  CloseHandle(h);
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
