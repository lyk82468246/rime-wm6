//
// wince_compat/X11/keysym.h -- minimal X11 keysym shim.
//
// Upstream rime's key_table.{h,cc} and key_event.cc all `#include
// <X11/keysym.h>` because the engine reuses X11's keysym numbering (XK_*).
// Auditing those three files shows only ONE XK_* macro is actually
// referenced -- `XK_VoidSymbol` -- and it's used as a "not found" sentinel
// by RimeGetKeycodeByName. Every other key code is hard-coded as an
// integer literal inside key_table.cc's static lookup tables.
//
// So instead of vendoring the full 1500-symbol keysymdef.h, this shim
// defines only what the codebase touches. If a future port surface needs
// XK_a / XK_Return / etc. as named constants (e.g. when ported gear/*
// processors enumerate hotkeys symbolically), we can copy in the upstream
// keysymdef.h from X.org at that point -- BSD-licensed, drop-in -- without
// touching this file's existing constant.
//
// The value 0xffffff is the X11-standard XK_VoidSymbol from
// X.org's keysymdef.h. key_table.cc's `keys_by_keyval` array uses it as
// the terminating sentinel, so the value must match the trailing entry
// in that table (which it does -- 0xffffff). DO NOT change.
//
#ifndef WINCE_COMPAT_X11_KEYSYM_H_
#define WINCE_COMPAT_X11_KEYSYM_H_

// X11 keysym values used by code paths beyond key_table.cc's lookup table.
// Add new constants here on-demand as new ported files reference them.
// All values come straight from X.org's keysymdef.h.

#define XK_BackSpace  0xff08
#define XK_Tab        0xff09
#define XK_Return     0xff0d
#define XK_Escape     0xff1b
#define XK_Delete     0xffff
#define XK_Home       0xff50
#define XK_Left       0xff51
#define XK_Up         0xff52
#define XK_Right      0xff53
#define XK_Down       0xff54
#define XK_Page_Up    0xff55
#define XK_Page_Down  0xff56
#define XK_End        0xff57
#define XK_Shift_L    0xffe1
#define XK_Shift_R    0xffe2
#define XK_Control_L  0xffe3
#define XK_Control_R  0xffe4
#define XK_space      0x0020

#define XK_VoidSymbol 0xffffff

#endif  // WINCE_COMPAT_X11_KEYSYM_H_
