//
// wmrime_sip/utf_conv.cc -- UTF-8 <-> UTF-16 via Win32 codepage APIs.
//
#include "utf_conv.h"

#include <windows.h>

namespace wmrime {

std::wstring Utf8ToUtf16(const char* s) {
  if (!s || !*s) return std::wstring();
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (wlen <= 1) return std::wstring();
  std::wstring out(static_cast<size_t>(wlen - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], wlen);
  return out;
}

std::wstring Utf8ToUtf16(const std::string& s) {
  return Utf8ToUtf16(s.c_str());
}

std::string Utf16ToUtf8(const wchar_t* ws) {
  if (!ws || !*ws) return std::string();
  int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
  if (len <= 1) return std::string();
  std::string out(static_cast<size_t>(len - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws, -1, &out[0], len, NULL, NULL);
  return out;
}

std::string Utf16ToUtf8(const std::wstring& ws) {
  return Utf16ToUtf8(ws.c_str());
}

}  // namespace wmrime
