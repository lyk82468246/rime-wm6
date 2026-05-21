//
// wince_compat/utf8.h -- minimal utfcpp subset, byte-level UTF-8 codec.
//
// Upstream librime uses utfcpp (Nemanja Trifunovic's UTF-8 CPP library), but
// the only entry points it actually touches in the MVP scope are
//   utf8::unchecked::next(iter)    -- read one code point, advance iter
//   utf8::unchecked::append(cp, p) -- write one code point, return new p
// so we inline a 50-line implementation here instead of vendoring the
// whole library. Matches utfcpp semantics: "unchecked" means we trust the
// input to be well-formed UTF-8 -- no validation, no error reporting --
// which is fine for rime schemas that have already been encoding-validated
// upstream of this layer.
//
// Header name (`utf8.h`) intentionally matches upstream. With wince_compat
// listed before librime/src on the include path, `#include <utf8.h>` from
// the ported calculus.cc resolves here, not to the upstream utfcpp.
//
#ifndef WINCE_COMPAT_UTF8_H_
#define WINCE_COMPAT_UTF8_H_

#include <stdint.h>

namespace utf8 {
namespace unchecked {

// Decode one UTF-8 code point starting at *p and advance p past it.
// Returns 0 when *p == 0 (callers loop until 0). Iter is typically
// `const char*` -- passed by reference so the caller's pointer moves.
template <class Iter>
inline uint32_t next(Iter& p) {
  unsigned char b0 = static_cast<unsigned char>(*p);
  if (b0 == 0) return 0;
  ++p;
  if (b0 < 0x80) {
    return b0;
  }
  if ((b0 & 0xE0) == 0xC0) {
    unsigned char b1 = static_cast<unsigned char>(*p); ++p;
    return ((static_cast<uint32_t>(b0) & 0x1F) << 6) |
           (static_cast<uint32_t>(b1) & 0x3F);
  }
  if ((b0 & 0xF0) == 0xE0) {
    unsigned char b1 = static_cast<unsigned char>(*p); ++p;
    unsigned char b2 = static_cast<unsigned char>(*p); ++p;
    return ((static_cast<uint32_t>(b0) & 0x0F) << 12) |
           ((static_cast<uint32_t>(b1) & 0x3F) << 6) |
           (static_cast<uint32_t>(b2) & 0x3F);
  }
  // 4-byte: U+10000..U+10FFFF
  unsigned char b1 = static_cast<unsigned char>(*p); ++p;
  unsigned char b2 = static_cast<unsigned char>(*p); ++p;
  unsigned char b3 = static_cast<unsigned char>(*p); ++p;
  return ((static_cast<uint32_t>(b0) & 0x07) << 18) |
         ((static_cast<uint32_t>(b1) & 0x3F) << 12) |
         ((static_cast<uint32_t>(b2) & 0x3F) << 6) |
         (static_cast<uint32_t>(b3) & 0x3F);
}

// Encode code point cp as UTF-8 starting at p, return the new write position.
// Iter is typically `char*`. Caller is responsible for buffer space (up to 4
// bytes per call); rime's call sites pre-allocate.
template <class Iter>
inline Iter append(uint32_t cp, Iter p) {
  if (cp < 0x80) {
    *p = static_cast<char>(cp); ++p;
  } else if (cp < 0x800) {
    *p = static_cast<char>(0xC0 | (cp >> 6)); ++p;
    *p = static_cast<char>(0x80 | (cp & 0x3F)); ++p;
  } else if (cp < 0x10000) {
    *p = static_cast<char>(0xE0 | (cp >> 12)); ++p;
    *p = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); ++p;
    *p = static_cast<char>(0x80 | (cp & 0x3F)); ++p;
  } else {
    *p = static_cast<char>(0xF0 | (cp >> 18)); ++p;
    *p = static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); ++p;
    *p = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); ++p;
    *p = static_cast<char>(0x80 | (cp & 0x3F)); ++p;
  }
  return p;
}

// Count code points between [first, last). Like utfcpp's utf8::distance.
// Used by the encoder to bound max_phrase_length checks on UTF-8 input.
// Iter is typically `const char*`; we walk it by calling next().
template <class Iter>
inline size_t distance(Iter first, Iter last) {
  size_t n = 0;
  while (first < last) {
    next(first);  // advances first past one code point
    ++n;
  }
  return n;
}

}  // namespace unchecked
}  // namespace utf8

#endif  // WINCE_COMPAT_UTF8_H_
