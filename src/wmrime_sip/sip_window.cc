//
// wmrime_sip/sip_window.cc -- panel drawing + hit-testing.
//
// Layout (panel rectangle 0..width x 0..height):
//
//   +------------------------------------------+
//   | preedit: "nihao"                         |  18 px
//   +------------------------------------------+
//   | 1 ni hao  2 ni  3 hao  4 ...             |  22 px
//   +------------------------------------------+
//   | q w e r t y u i o p                      |  rest
//   |  a s d f g h j k l                       |  / 3
//   | z x c v b n m  back  ret                 |
//   |          space                           |  util row at bottom
//   +------------------------------------------+
//
// Touch geometry is computed from `width`/`height` so different SIP
// sizes (PocketPC 240 vs VGA 480) lay out cleanly.
//
#include "sip_window.h"

#include <windows.h>
#include <stdlib.h>

#include <rime_api.h>

#include "utf_conv.h"

namespace wmrime {

namespace {

const int kPreeditHeight    = 18;
const int kCandidateHeight  = 22;

// X11 keysyms we send to rime.
const int kKey_BackSpace = 0xff08;
const int kKey_Return    = 0xff0d;
const int kKey_Escape    = 0xff1b;
const int kKey_Space     = 0x0020;

// Standard QWERTY rows we lay out as a soft keyboard.
const wchar_t* kRow0 = L"qwertyuiop";
const wchar_t* kRow1 = L"asdfghjkl";
const wchar_t* kRow2 = L"zxcvbnm";

// Labels for the util row (after the z-m letters).
struct UtilKey {
  const wchar_t* label;
  int keycode;
};
const UtilKey kUtilKeys[] = {
  { L"BS",  kKey_BackSpace },
  { L"Esc", kKey_Escape },
  { L"Ret", kKey_Return },
};

void FillSolid(HDC hdc, const RECT* r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(hdc, r, b);
  DeleteObject(b);
}

void DrawText16(HDC hdc, const RECT* r, const wchar_t* txt, UINT format) {
  if (!txt || !*txt) return;
  RECT tmp = *r;
  DrawTextW(hdc, txt, -1, &tmp, format);
}

void DrawCellBorder(HDC hdc, const RECT* r) {
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
  HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
  HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
  Rectangle(hdc, r->left, r->top, r->right, r->bottom);
  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBr);
  DeleteObject(pen);
}

}  // namespace

void RecomputeLayout(PanelState* st, int width, int height) {
  st->panel_width = width;
  st->panel_height = height;
  st->keys.clear();
  st->candidates.clear();

  // Top strips.
  st->preedit_rect.left = 0;
  st->preedit_rect.top = 0;
  st->preedit_rect.right = width;
  st->preedit_rect.bottom = kPreeditHeight;

  st->candidates_rect.left = 0;
  st->candidates_rect.top = kPreeditHeight;
  st->candidates_rect.right = width;
  st->candidates_rect.bottom = kPreeditHeight + kCandidateHeight;

  // Keyboard region: remainder. Divided into 4 rows: kRow0, kRow1,
  // kRow2 + util keys, and a separate bottom row for the space bar.
  int kb_top = st->candidates_rect.bottom;
  st->keyboard_rect.left = 0;
  st->keyboard_rect.top = kb_top;
  st->keyboard_rect.right = width;
  st->keyboard_rect.bottom = height;

  int kb_h = height - kb_top;
  if (kb_h < 4) return;  // panel too small for keyboard

  int row_h = kb_h / 4;
  if (row_h < 1) row_h = 1;

  // Row 0 (10 keys)
  int n0 = static_cast<int>(wcslen(kRow0));
  int key_w0 = width / n0;
  for (int i = 0; i < n0; ++i) {
    SoftKey k;
    k.rect.left = i * key_w0;
    k.rect.top = kb_top + row_h * 0;
    k.rect.right = (i + 1) * key_w0;
    k.rect.bottom = kb_top + row_h * 1;
    static wchar_t s_buf0[11];  // shared across keys; replace per-key
    // safer: store the char in keycode and let paint render.
    k.keycode = kRow0[i];
    k.label = NULL;  // will render from keycode at paint time
    (void)s_buf0;
    st->keys.push_back(k);
  }

  // Row 1 (9 keys), offset by half a key width for the staggered look.
  int n1 = static_cast<int>(wcslen(kRow1));
  int key_w1 = width / (n1 + 1);  // make the row narrower to inset
  int x_offset1 = key_w1 / 2;
  for (int i = 0; i < n1; ++i) {
    SoftKey k;
    k.rect.left = x_offset1 + i * key_w1;
    k.rect.top = kb_top + row_h * 1;
    k.rect.right = x_offset1 + (i + 1) * key_w1;
    k.rect.bottom = kb_top + row_h * 2;
    k.keycode = kRow1[i];
    k.label = NULL;
    st->keys.push_back(k);
  }

  // Row 2 (7 keys) + util-row keys (3 keys) on the same line.
  int n2 = static_cast<int>(wcslen(kRow2));
  int util_n = sizeof(kUtilKeys) / sizeof(kUtilKeys[0]);
  int slots2 = n2 + util_n;
  int key_w2 = width / slots2;
  for (int i = 0; i < n2; ++i) {
    SoftKey k;
    k.rect.left = i * key_w2;
    k.rect.top = kb_top + row_h * 2;
    k.rect.right = (i + 1) * key_w2;
    k.rect.bottom = kb_top + row_h * 3;
    k.keycode = kRow2[i];
    k.label = NULL;
    st->keys.push_back(k);
  }
  for (int i = 0; i < util_n; ++i) {
    SoftKey k;
    k.rect.left = (n2 + i) * key_w2;
    k.rect.top = kb_top + row_h * 2;
    k.rect.right = (n2 + i + 1) * key_w2;
    k.rect.bottom = kb_top + row_h * 3;
    k.keycode = kUtilKeys[i].keycode;
    k.label = kUtilKeys[i].label;
    st->keys.push_back(k);
  }

  // Bottom row: space bar spans whole width.
  {
    SoftKey k;
    k.rect.left = 0;
    k.rect.top = kb_top + row_h * 3;
    k.rect.right = width;
    k.rect.bottom = height;
    k.keycode = kKey_Space;
    k.label = L"space";
    st->keys.push_back(k);
  }
}

void RefreshFromRime(PanelState* st) {
  st->last_preedit.clear();
  st->candidates.clear();
  if (!st->session) return;

  RimeContext ctx;
  ZeroMemory(&ctx, sizeof(ctx));
  if (!RimeGetContext(st->session, &ctx)) return;

  if (ctx.composition.preedit && *ctx.composition.preedit) {
    st->last_preedit = Utf8ToUtf16(ctx.composition.preedit);
  }

  // Layout candidates left-to-right inside candidates_rect. We size
  // each cell proportionally to its text length so a long phrase
  // gets enough room.
  int n = ctx.menu.num_candidates;
  if (n > 0 && ctx.menu.candidates) {
    // Sum desired widths; fall back to equal partition if total > rect.
    int total_chars = 0;
    int per_char = 12;  // rough px per CJK glyph at default font
    for (int i = 0; i < n; ++i) {
      const char* t = ctx.menu.candidates[i].text;
      // Each candidate gets at least 32 px for "N text" + padding.
      int desired = 32;
      if (t) {
        // Count bytes -- rough heuristic; CJK is 3 bytes/glyph in UTF-8.
        int len = 0;
        for (const char* p = t; *p; ++p) ++len;
        desired = 32 + (len / 3) * per_char;
      }
      total_chars += desired;
    }
    int rect_w = st->candidates_rect.right - st->candidates_rect.left;
    if (total_chars > rect_w) total_chars = rect_w;
    int x = st->candidates_rect.left;
    for (int i = 0; i < n; ++i) {
      CandidateSlot slot;
      const char* t = ctx.menu.candidates[i].text;
      int desired = 32;
      if (t) {
        int len = 0;
        for (const char* p = t; *p; ++p) ++len;
        desired = 32 + (len / 3) * per_char;
      }
      if (desired > rect_w / 2) desired = rect_w / 2;
      slot.rect.left = x;
      slot.rect.top = st->candidates_rect.top;
      slot.rect.right = x + desired;
      slot.rect.bottom = st->candidates_rect.bottom;
      slot.absolute_index = ctx.menu.page_no * ctx.menu.page_size + i;
      st->candidates.push_back(slot);
      x += desired;
      if (x >= st->candidates_rect.right) break;
    }
  }

  RimeFreeContext(&ctx);
}

void PaintPanel(HDC hdc, const PanelState* st) {
  // Background fill.
  RECT full;
  full.left = 0; full.top = 0;
  full.right = st->panel_width; full.bottom = st->panel_height;
  FillSolid(hdc, &full, RGB(245, 245, 245));

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(20, 20, 20));

  // Preedit row.
  FillSolid(hdc, &st->preedit_rect, RGB(255, 255, 255));
  DrawText16(hdc, &st->preedit_rect,
             st->last_preedit.empty() ? L"" : st->last_preedit.c_str(),
             DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  // Candidates row.
  FillSolid(hdc, &st->candidates_rect, RGB(230, 230, 230));
  if (!st->candidates.empty() && st->session) {
    RimeContext ctx;
    ZeroMemory(&ctx, sizeof(ctx));
    if (RimeGetContext(st->session, &ctx)) {
      int n = static_cast<int>(st->candidates.size());
      if (n > ctx.menu.num_candidates) n = ctx.menu.num_candidates;
      for (int i = 0; i < n; ++i) {
        const RECT* r = &st->candidates[i].rect;
        const char* t = ctx.menu.candidates[i].text;
        wchar_t buf[128];
        std::wstring w = t ? Utf8ToUtf16(t) : std::wstring();
        wsprintfW(buf, L"%d %s", i + 1, w.c_str());
        DrawText16(hdc, r, buf,
                   DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (i == ctx.menu.highlighted_candidate_index) {
          // Underline the highlighted one.
          RECT hr = *r;
          hr.top = hr.bottom - 2;
          FillSolid(hdc, &hr, RGB(50, 100, 200));
        }
      }
      RimeFreeContext(&ctx);
    }
  }

  // Keyboard region: paint each key.
  for (size_t i = 0; i < st->keys.size(); ++i) {
    const SoftKey& k = st->keys[i];
    FillSolid(hdc, &k.rect, RGB(255, 255, 255));
    DrawCellBorder(hdc, &k.rect);
    wchar_t glyph[8];
    const wchar_t* label = k.label;
    if (!label) {
      // Single-char letter key.
      glyph[0] = static_cast<wchar_t>(k.keycode);
      glyph[1] = L'\0';
      label = glyph;
    }
    DrawText16(hdc, &k.rect, label,
               DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
}

HitResult HandleTap(PanelState* st, int x, int y,
                    std::string* commit_text) {
  if (commit_text) commit_text->clear();
  if (!st->session) return kHitNothing;

  // Candidate strip first (above keyboard).
  for (size_t i = 0; i < st->candidates.size(); ++i) {
    const RECT* r = &st->candidates[i].rect;
    if (x >= r->left && x < r->right && y >= r->top && y < r->bottom) {
      // Select that candidate within current page. The 0-based index
      // matches our candidate slot list.
      RimeSelectCandidateOnCurrentPage(st->session, i);
      if (commit_text) *commit_text = DrainCommit(st->session);
      return kHitCandidate;
    }
  }

  // Keyboard keys.
  for (size_t i = 0; i < st->keys.size(); ++i) {
    const SoftKey& k = st->keys[i];
    if (x >= k.rect.left && x < k.rect.right &&
        y >= k.rect.top && y < k.rect.bottom) {
      RimeProcessKey(st->session, k.keycode, 0);
      if (commit_text) *commit_text = DrainCommit(st->session);
      return kHitKey;
    }
  }
  return kHitNothing;
}

std::string DrainCommit(RimeSessionId session) {
  if (!session) return std::string();
  RimeCommit commit;
  ZeroMemory(&commit, sizeof(commit));
  if (!RimeGetCommit(session, &commit)) return std::string();
  std::string out;
  if (commit.text) out = commit.text;
  RimeFreeCommit(&commit);
  return out;
}

}  // namespace wmrime
