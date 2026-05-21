//
// wince_compat/path.h -- minimal std::filesystem::path replacement.
//
// Stores a UTF-8 string internally. Converts to UTF-16 on demand via utf.h
// when calling Win32 APIs. Implements only the path-manipulation surface that
// the MVP librime runtime touches:
//
//   ctor(const char* utf8), ctor(const std::string& utf8)
//   string() / u8string()   -> std::string  (UTF-8, identical)
//   wstring()               -> std::wstring (UTF-16)
//   c_str()                 -> const char*  (UTF-8 null-terminated)
//   empty()
//   operator/=, operator/   (path or std::string, treated as UTF-8)
//   filename(), parent_path(), extension(), stem()
//   remove_filename(), replace_extension()
//
//   exists(const path&)        free function, GetFileAttributesW backed
//   is_directory(const path&)  free function
//
// All other std::filesystem features (canonical, absolute, directory_iterator,
// last_write_time, ...) are deliberately omitted -- the librime call sites
// that needed them live in dict_compiler / deployment_tasks, which the WinCE
// MVP cuts entirely. Add piece-meal if a kept source file needs more.
//
#ifndef WINCE_COMPAT_PATH_H_
#define WINCE_COMPAT_PATH_H_

#include <windows.h>
#include <string>

#include "utf.h"

namespace wince {

class path {
 public:
  path() {}
  path(const char* utf8)        : s_(utf8 ? utf8 : "") {}
  path(const std::string& utf8) : s_(utf8) {}

  // Conversions
  const std::string& string()   const { return s_; }
  const std::string& u8string() const { return s_; }
  const char*        c_str()    const { return s_.c_str(); }
  std::wstring       wstring()  const { return utf8_to_utf16(s_); }

  bool empty() const { return s_.empty(); }

  // Concatenation. We always use backslash as the separator on WinCE.
  // Edge cases handled: empty LHS, LHS already ending in separator, RHS
  // starting with separator. A leading separator on RHS means "absolute" in
  // std::filesystem semantics -- we mirror that by REPLACING the LHS.
  path& operator/=(const path& rhs) { return append(rhs.s_); }
  path& operator/=(const std::string& rhs) { return append(rhs); }
  path& operator/=(const char* rhs) { return append(rhs ? rhs : ""); }

  friend path operator/(const path& lhs, const path& rhs) {
    path out(lhs); out /= rhs; return out;
  }
  friend path operator/(const path& lhs, const std::string& rhs) {
    path out(lhs); out /= rhs; return out;
  }
  friend path operator/(const path& lhs, const char* rhs) {
    path out(lhs); out /= rhs; return out;
  }

  // Component accessors. Separators recognised: '\\' (WinCE native) and '/'
  // (frequent in rime data files; tolerated for portability).
  path filename() const {
    std::string::size_type pos = last_sep();
    return pos == std::string::npos ? path(s_)
                                     : path(s_.substr(pos + 1));
  }
  path parent_path() const {
    std::string::size_type pos = last_sep();
    return pos == std::string::npos ? path() : path(s_.substr(0, pos));
  }
  path extension() const {
    std::string fn = filename().s_;
    std::string::size_type dot = fn.rfind('.');
    // Special-case ".", ".." -- they have no extension.
    if (dot == std::string::npos || dot == 0 || fn == "." || fn == "..")
      return path();
    return path(fn.substr(dot));
  }
  path stem() const {
    std::string fn = filename().s_;
    std::string::size_type dot = fn.rfind('.');
    if (dot == std::string::npos || dot == 0 || fn == "." || fn == "..")
      return path(fn);
    return path(fn.substr(0, dot));
  }

  // Mutators
  path& remove_filename() {
    std::string::size_type pos = last_sep();
    s_ = (pos == std::string::npos) ? std::string() : s_.substr(0, pos + 1);
    return *this;
  }
  path& replace_extension(const path& ext) {
    std::string::size_type fn_pos = last_sep();
    std::string fn = (fn_pos == std::string::npos) ? s_ : s_.substr(fn_pos + 1);
    std::string::size_type dot = fn.rfind('.');
    if (dot != std::string::npos && dot != 0 && fn != "." && fn != "..") {
      // strip existing extension
      s_.resize((fn_pos == std::string::npos ? 0 : fn_pos + 1) + dot);
    }
    if (!ext.s_.empty()) {
      if (ext.s_[0] != '.') s_ += '.';
      s_ += ext.s_;
    }
    return *this;
  }

 private:
  path& append(const std::string& rhs) {
    if (rhs.empty()) return *this;
    if (is_sep(rhs[0])) {
      // Absolute-on-rhs: replace lhs entirely.
      s_ = rhs;
      return *this;
    }
    if (!s_.empty() && !is_sep(s_[s_.size() - 1])) s_ += '\\';
    s_ += rhs;
    return *this;
  }
  static bool is_sep(char c) { return c == '\\' || c == '/'; }
  std::string::size_type last_sep() const {
    std::string::size_type a = s_.rfind('\\');
    std::string::size_type b = s_.rfind('/');
    if (a == std::string::npos) return b;
    if (b == std::string::npos) return a;
    return a > b ? a : b;
  }

  std::string s_;  // UTF-8
};

inline bool operator==(const path& a, const path& b) {
  return a.string() == b.string();
}
inline bool operator!=(const path& a, const path& b) {
  return a.string() != b.string();
}

// Free-function filesystem queries. Implemented with GetFileAttributesW so we
// can avoid pulling in <io.h> / _wstat.
inline bool exists(const path& p) {
  if (p.empty()) return false;
  DWORD attr = GetFileAttributesW(p.wstring().c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}
inline bool is_directory(const path& p) {
  if (p.empty()) return false;
  DWORD attr = GetFileAttributesW(p.wstring().c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
inline bool is_regular_file(const path& p) {
  if (p.empty()) return false;
  DWORD attr = GetFileAttributesW(p.wstring().c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}  // namespace wince

#endif  // WINCE_COMPAT_PATH_H_
