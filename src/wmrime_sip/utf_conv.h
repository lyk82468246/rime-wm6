//
// wmrime_sip/utf_conv.h -- thin UTF-8 <-> UTF-16 helpers.
//
// librime/RimeCore uses char* (UTF-8) everywhere; Win32/COM on WinCE
// uses wchar_t* (UTF-16). Conversion is confined to this layer + the
// rime_api boundary. Wraps MultiByteToWideChar / WideCharToMultiByte
// with std::wstring/std::string returns so callers don't manage buffers.
//
#ifndef WMRIME_SIP_UTF_CONV_H_
#define WMRIME_SIP_UTF_CONV_H_

#include <string>

namespace wmrime {

// UTF-8 char* -> UTF-16 wstring. Returns empty string on null/empty input.
std::wstring Utf8ToUtf16(const char* s);
std::wstring Utf8ToUtf16(const std::string& s);

// UTF-16 wchar_t* -> UTF-8 string.
std::string Utf16ToUtf8(const wchar_t* ws);
std::string Utf16ToUtf8(const std::wstring& ws);

}  // namespace wmrime

#endif  // WMRIME_SIP_UTF_CONV_H_
