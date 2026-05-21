//
// rime/key_table.h -- WinCE-port mirror of upstream key_table.h.
//
// Changes vs. upstream:
//   * Three CJK comments translated to ASCII (file-encoding rule).
//   * Otherwise byte-identical.
//
// X11/keysym.h resolves to our 1-macro shim (src/wince_compat/X11/keysym.h)
// via the include path. WinCE has no X11; rime just reuses X11's key
// numbering scheme.
//
#ifndef RIME_KEY_TABLE_H_
#define RIME_KEY_TABLE_H_

#include <X11/keysym.h>
#include <rime_api.h>

typedef enum {
  kShiftMask = 1 << 0,
  kLockMask = 1 << 1,
  kControlMask = 1 << 2,
  kMod1Mask = 1 << 3,
  kAltMask = kMod1Mask,
  kMod2Mask = 1 << 4,
  kMod3Mask = 1 << 5,
  kMod4Mask = 1 << 6,
  kMod5Mask = 1 << 7,
  kButton1Mask = 1 << 8,
  kButton2Mask = 1 << 9,
  kButton3Mask = 1 << 10,
  kButton4Mask = 1 << 11,
  kButton5Mask = 1 << 12,

  /* The next few modifiers are used by XKB, so we skip to the end.
   * Bits 15 - 23 are currently unused. Bit 29 is used internally.
   */

  /* ibus :) mask */
  kHandledMask = 1 << 24,
  kForwardMask = 1 << 25,
  kIgnoredMask = kForwardMask,

  kSuperMask = 1 << 26,
  kHyperMask = 1 << 27,
  kMetaMask = 1 << 28,

  kReleaseMask = 1 << 30,

  kModifierMask = 0x5f001fff
} RimeModifier;

// Map a modifier name string to its bitmask value.
//   e.g. RimeGetModifierByName("Alt") == (1 << 3)
// Unknown name -> 0.
RIME_DLL int RimeGetModifierByName(const char* name);

// Given a modifier bitmask, return the name of the lowest-set bit.
//   e.g. RimeGetModifierName(12) == "Control"
// Returns NULL if no name maps to this bit.
RIME_DLL const char* RimeGetModifierName(int modifier);

// Look up keycode by key name. Returns XK_VoidSymbol if unknown.
RIME_DLL int RimeGetKeycodeByName(const char* name);

// Inverse: keycode to name. Returns NULL if unknown.
RIME_DLL const char* RimeGetKeyName(int keycode);

#endif  // RIME_KEY_TABLE_H_
