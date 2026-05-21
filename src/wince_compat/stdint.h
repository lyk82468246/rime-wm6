//
// wince_compat/stdint.h -- MSVC 9.0 doesn't ship <stdint.h> (it landed in
// VS2010). Upstream librime headers (calculus.h, algo/encoder.h, dict/*)
// freely #include <stdint.h> for uint32_t / int64_t, so we provide our
// own shim. With src/wince_compat first on the include path, an
// `#include <stdint.h>` from anywhere resolves here.
//
// Types are mapped to MSVC's built-in fixed-width compiler intrinsics
// (__int8 .. __int64 + signed/unsigned), which on ARMV4I / WinCE match the
// widths exactly. intptr_t / uintptr_t are 32-bit on this target.
//
#ifndef WINCE_COMPAT_STDINT_H_
#define WINCE_COMPAT_STDINT_H_

typedef signed __int8       int8_t;
typedef unsigned __int8     uint8_t;
typedef signed __int16      int16_t;
typedef unsigned __int16    uint16_t;
typedef signed __int32      int32_t;
typedef unsigned __int32    uint32_t;
typedef signed __int64      int64_t;
typedef unsigned __int64    uint64_t;

// "Fast" / "least" variants -- on this target same widths as the canonical
// fixed-width types. Provided for completeness; rime doesn't use them.
typedef int8_t              int_least8_t;
typedef uint8_t             uint_least8_t;
typedef int16_t             int_least16_t;
typedef uint16_t            uint_least16_t;
typedef int32_t             int_least32_t;
typedef uint32_t            uint_least32_t;
typedef int64_t             int_least64_t;
typedef uint64_t            uint_least64_t;

typedef int8_t              int_fast8_t;
typedef uint8_t             uint_fast8_t;
typedef int16_t             int_fast16_t;
typedef uint16_t            uint_fast16_t;
typedef int32_t             int_fast32_t;
typedef uint32_t            uint_fast32_t;
typedef int64_t             int_fast64_t;
typedef uint64_t            uint_fast64_t;

typedef int64_t             intmax_t;
typedef uint64_t            uintmax_t;

// ARMV4I / WinCE: 32-bit pointers.
typedef int32_t             intptr_t;
typedef uint32_t            uintptr_t;

#define INT8_MIN     (-127i8 - 1)
#define INT16_MIN    (-32767i16 - 1)
#define INT32_MIN    (-2147483647i32 - 1)
#define INT64_MIN    (-9223372036854775807i64 - 1)
#define INT8_MAX     127i8
#define INT16_MAX    32767i16
#define INT32_MAX    2147483647i32
#define INT64_MAX    9223372036854775807i64
#define UINT8_MAX    0xffui8
#define UINT16_MAX   0xffffui16
#define UINT32_MAX   0xffffffffui32
#define UINT64_MAX   0xffffffffffffffffui64

#define INTPTR_MIN   INT32_MIN
#define INTPTR_MAX   INT32_MAX
#define UINTPTR_MAX  UINT32_MAX
#define INTMAX_MIN   INT64_MIN
#define INTMAX_MAX   INT64_MAX
#define UINTMAX_MAX  UINT64_MAX

// Literal-suffix macros (C99). Rime uses INT64_C / UINT64_C in a couple of
// dict/marisa-trie spots downstream; provide them for parity.
#define INT8_C(x)    (x##i8)
#define INT16_C(x)   (x##i16)
#define INT32_C(x)   (x##i32)
#define INT64_C(x)   (x##i64)
#define UINT8_C(x)   (x##ui8)
#define UINT16_C(x)  (x##ui16)
#define UINT32_C(x)  (x##ui32)
#define UINT64_C(x)  (x##ui64)

#endif  // WINCE_COMPAT_STDINT_H_
