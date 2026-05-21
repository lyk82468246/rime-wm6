//
// wince_compat/utf.h -- UTF-8 <-> UTF-16 conversion helpers for the WinCE port.
//
// Why this exists: librime stores all strings as UTF-8 in std::string. WinCE's
// Win32 API (CreateFileW, FindFirstFileW, etc.) and COM interfaces accept only
// wchar_t (UTF-16). All conversions are funnelled through this header so that
// encoding boundaries are explicit and grep-able.
//
// Implementation note: WinCE's MultiByteToWideChar / WideCharToMultiByte are
// declared in <windows.h>. CP_UTF8 is supported on WinCE 4.0+ (we target 6.0).
// MB_ERR_INVALID_CHARS is silently ignored on some WinCE builds; we pass 0 and
// rely on the caller having well-formed input (UTF-8 comes from rime data
// files we control).
//
#ifndef WINCE_COMPAT_UTF_H_
#define WINCE_COMPAT_UTF_H_

#include <windows.h>
#include <string>

namespace wince {

inline std::wstring utf8_to_utf16(const char* utf8, int byte_len) {
  if (!utf8 || byte_len == 0) return std::wstring();
  int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, byte_len, NULL, 0);
  if (wide_len <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(wide_len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, byte_len, &out[0], wide_len);
  return out;
}

inline std::wstring utf8_to_utf16(const std::string& s) {
  // We pass static_cast<int>(s.size()) (NOT -1) so the result does NOT include
  // a terminating null in its length, matching std::wstring semantics.
  return utf8_to_utf16(s.data(), static_cast<int>(s.size()));
}

inline std::string utf16_to_utf8(const wchar_t* utf16, int wide_len) {
  if (!utf16 || wide_len == 0) return std::string();
  int byte_len = WideCharToMultiByte(CP_UTF8, 0, utf16, wide_len,
                                     NULL, 0, NULL, NULL);
  if (byte_len <= 0) return std::string();
  std::string out(static_cast<size_t>(byte_len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, utf16, wide_len,
                      &out[0], byte_len, NULL, NULL);
  return out;
}

inline std::string utf16_to_utf8(const std::wstring& s) {
  return utf16_to_utf8(s.data(), static_cast<int>(s.size()));
}

}  // namespace wince

#endif  // WINCE_COMPAT_UTF_H_
