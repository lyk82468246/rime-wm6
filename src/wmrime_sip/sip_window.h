//
// wmrime_sip/sip_window.h -- UI state + free-function operations on
// the SIP panel. Drawing, hit-testing, and rime_api integration live
// here; rime_input_method.cc just owns the lifetime.
//
// Panel layout (top to bottom):
//   * top strip       -- preedit text (the raw pinyin so far)
//   * middle strip    -- numbered candidates "1 ni 2 hao 3 ..."
//   * bottom region   -- soft QWERTY keyboard (3 letter rows + util row)
//
// We assume a panel width >= 200 px and height >= 100 px. Smaller
// panels (e.g. landscape mini-SIP) still paint, but keys may overlap.
//
#ifndef WMRIME_SIP_SIP_WINDOW_H_
#define WMRIME_SIP_SIP_WINDOW_H_

#include <stdint.h>
#include <windows.h>

#include <rime_api.h>

#include <string>
#include <vector>

namespace wmrime {

// One on-screen key.
struct SoftKey {
  RECT rect;        // panel-local rect
  int keycode;      // X11 keysym we pass to RimeProcessKey (e.g. 'a',
                    //   0xff08 BackSpace, 0x20 space, 0xff0d Return,
                    //   0xff1b Escape, '\'' apostrophe, ',' '.', etc.)
  const wchar_t* label;  // what to draw on the key
};

// One candidate slot's hit rect (filled by RecomputeLayout, consumed
// by hit-testing).
struct CandidateSlot {
  RECT rect;        // panel-local rect of the whole "N text" cell
  int absolute_index;  // index into the all-candidates list (paged)
};

struct PanelState {
  RimeSessionId session;        // rime_api session
  int panel_width;
  int panel_height;
  // Layout-derived geometry. Recomputed by RecomputeLayout().
  RECT preedit_rect;
  RECT candidates_rect;
  RECT keyboard_rect;
  std::vector<SoftKey> keys;
  std::vector<CandidateSlot> candidates;
  std::wstring last_preedit;        // cached for paint
  std::wstring last_commit_preview; // unused for now
};

// Initialise / reset for a fresh panel size. Recreates keys + lays out
// the candidate strip. Call once per ReceiveSipInfo and whenever the
// panel HWND is resized.
void RecomputeLayout(PanelState* st, int width, int height);

// Pull the current preedit + candidate list out of rime_api and stash
// it in the PanelState so the next WM_PAINT renders correctly.
void RefreshFromRime(PanelState* st);

// Paint the whole panel into hdc. Caller already obtained the DC via
// BeginPaint / GetDC.
void PaintPanel(HDC hdc, const PanelState* st);

// Handle a tap. x/y are panel-local coordinates. Returns:
//   kHitNothing      -- no key / candidate matched
//   kHitKey          -- a soft key was tapped; key_consumed/repaint set
//   kHitCandidate    -- a candidate was tapped; the chosen text is in
//                       *commit_text (UTF-8) and the caller should send
//                       it back through IIMCallback.
enum HitResult {
  kHitNothing,
  kHitKey,
  kHitCandidate,
};

HitResult HandleTap(PanelState* st, int x, int y,
                    std::string* commit_text);

// Take whatever's pending in the session's commit buffer, return it
// as a UTF-8 string (empty if nothing). Used by HandleTap to drain
// after a soft-key tap and by Showing/Hiding to push residual text.
std::string DrainCommit(RimeSessionId session);

}  // namespace wmrime

#endif  // WMRIME_SIP_SIP_WINDOW_H_
