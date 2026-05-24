//
// wmrime_sip/rime_input_method.cc -- IInputMethod2 + panel HWND wiring.
//
#include "rime_input_method.h"

#include <map>
#include <new>
#include <string>

#include "sip_globals.h"
#include "sip_window.h"
#include "utf_conv.h"
#include "log.h"

#include <rime_api.h>

namespace wmrime {

namespace {

// WinCE has no GetPropW/SetPropW. Use a process-local HWND -> instance
// map guarded by a CRITICAL_SECTION. The map is small (usually one
// entry per process) so a std::map is fine.
CRITICAL_SECTION g_panel_map_cs;
bool g_panel_map_cs_ready = false;

void EnsurePanelMapCs() {
  if (!g_panel_map_cs_ready) {
    InitializeCriticalSection(&g_panel_map_cs);
    g_panel_map_cs_ready = true;
  }
}

std::map<HWND, RimeInputMethod*>& panel_map() {
  static std::map<HWND, RimeInputMethod*> m;
  return m;
}

void RegisterPanel(HWND h, RimeInputMethod* self) {
  EnsurePanelMapCs();
  EnterCriticalSection(&g_panel_map_cs);
  panel_map()[h] = self;
  LeaveCriticalSection(&g_panel_map_cs);
}

void UnregisterPanel(HWND h) {
  if (!g_panel_map_cs_ready) return;
  EnterCriticalSection(&g_panel_map_cs);
  panel_map().erase(h);
  LeaveCriticalSection(&g_panel_map_cs);
}

RimeInputMethod* LookupPanel(HWND h) {
  if (!g_panel_map_cs_ready) return NULL;
  EnterCriticalSection(&g_panel_map_cs);
  std::map<HWND, RimeInputMethod*>::iterator it = panel_map().find(h);
  RimeInputMethod* p = (it == panel_map().end()) ? NULL : it->second;
  LeaveCriticalSection(&g_panel_map_cs);
  return p;
}

// Window class name uses our CLSID to guarantee global uniqueness even
// if multiple SIPs end up in the same hosting process. The official
// Dvorak sample does the same trick.
const wchar_t* kWindowClassName = L"WMRime.7B9F6D8E-4A2C-4F1E-9D6B-3E5A8C2D1F47";
bool g_class_registered = false;
CRITICAL_SECTION g_class_cs;
bool g_class_cs_ready = false;

void EnsureClassRegistered() {
  if (g_class_registered) return;
  if (!g_class_cs_ready) {
    InitializeCriticalSection(&g_class_cs);
    g_class_cs_ready = true;
  }
  EnterCriticalSection(&g_class_cs);
  if (!g_class_registered) {
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = RimeInputMethod::PanelWndProc;
    wc.hInstance     = GetSipModule();
    wc.hbrBackground = NULL;   // we paint background ourselves in WM_PAINT
    wc.lpszClassName = kWindowClassName;
    ATOM atom = RegisterClassW(&wc);
    if (atom) {
      g_class_registered = true;
      LogLineHex("EnsureClassRegistered: RegisterClass atom=", static_cast<unsigned int>(atom));
    } else {
      LogLineHex("EnsureClassRegistered: RegisterClass FAILED err=", GetLastError());
    }
  }
  LeaveCriticalSection(&g_class_cs);
}

// Diagnostic helper -- dump a RECT in the form "tag L=.. T=.. R=.. B=..".
// We split into separate LogLineInt calls because the existing log
// helpers don't have a multi-value variant; each line gets its own
// timestamp but that's fine for a one-shot probe.
void LogRect(const char* tag, const RECT& r) {
  LogLine(tag);
  LogLineInt("    .left   = ", r.left);
  LogLineInt("    .top    = ", r.top);
  LogLineInt("    .right  = ", r.right);
  LogLineInt("    .bottom = ", r.bottom);
}

}  // namespace

RimeInputMethod::RimeInputMethod()
    : ref_count_(1),
      panel_hwnd_(NULL),
      child_hwnd_(NULL),
      callback_(NULL),
      visible_(false) {
  InterlockedIncrement(&g_object_count);
  ZeroMemory(&panel_, sizeof(panel_));
  panel_.session = 0;
  LogLine("RimeInputMethod: ctor");
}

RimeInputMethod::~RimeInputMethod() {
  LogLine("RimeInputMethod: dtor");
  if (callback_) callback_->Release();
  if (panel_.session) {
    RimeDestroySession(panel_.session);
    panel_.session = 0;
  }
  InterlockedDecrement(&g_object_count);
}

STDMETHODIMP RimeInputMethod::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_IInputMethod)) {
    *ppv = static_cast<IInputMethod*>(this);
    AddRef();
    LogLine("RimeInputMethod::QI -> IInputMethod");
    return S_OK;
  }
  if (IsEqualIID(riid, IID_IInputMethod2)) {
    *ppv = static_cast<IInputMethod2*>(this);
    AddRef();
    LogLine("RimeInputMethod::QI -> IInputMethod2");
    return S_OK;
  }
  LogLine("RimeInputMethod::QI -> E_NOINTERFACE");
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) RimeInputMethod::AddRef() {
  return InterlockedIncrement(&ref_count_);
}

STDMETHODIMP_(ULONG) RimeInputMethod::Release() {
  LONG c = InterlockedDecrement(&ref_count_);
  if (c == 0) delete this;
  return static_cast<ULONG>(c);
}

// ----------------------------------------------------------------------
// IInputMethod
// ----------------------------------------------------------------------

STDMETHODIMP RimeInputMethod::Select(HWND hwndSip) {
  LogLinePtr("Select entered, hwndSip=", hwndSip);
  if (!hwndSip) {
    LogLine("Select: NULL hwndSip, returning E_INVALIDARG");
    return E_INVALIDARG;
  }
  EnsureRimeInitialized();
  EnsureClassRegistered();

  panel_hwnd_ = hwndSip;
  panel_.session = RimeCreateSession();
  LogLineInt("Select: RimeCreateSession returned id=", static_cast<int>(panel_.session));

  // Mirror the Microsoft Dvorak SIP sample (Windows Mobile 6 SDK,
  // Samples\PocketPC\CPP\ATL\dvoraksip\dvorak_implementation.cpp):
  //   - Use OUR registered window class (lpfnWndProc = PanelWndProc)
  //     instead of subclassing a predefined "STATIC" control. The
  //     framework appears to expect a custom-class child here; with
  //     a subclassed STATIC, our WM_PAINT was reaching us but the
  //     framework still suppressed the rendered output (no visible
  //     pixels even after BeginPaint/EndPaint succeeded).
  //   - CreateWindow with WS_CHILD ONLY (no WS_VISIBLE at create
  //     time). Dvorak uses initial 10x10; ReceiveSipInfo will resize
  //     it momentarily.
  //   - Then ShowWindow(SW_SHOWNOACTIVATE) -- shows the window
  //     without stealing focus or activation from the host app, which
  //     is what the SIP framework expects.
  child_hwnd_ = CreateWindowW(kWindowClassName, L"",
                              WS_CHILD,
                              0, 0, 10, 10,
                              panel_hwnd_, NULL, GetSipModule(), NULL);
  if (!child_hwnd_) {
    LogLineHex("Select: CreateWindow FAILED err=", GetLastError());
    return E_FAIL;
  }
  LogLinePtr("Select: created child_hwnd_=", child_hwnd_);

  RegisterPanel(child_hwnd_, this);
  ShowWindow(child_hwnd_, SW_SHOWNOACTIVATE);
  LogLine("Select: ShowWindow(SW_SHOWNOACTIVATE) done");

  // Force ourselves to the top of the sibling z-order under panel_hwnd_,
  // in case the framework also parks STATIC/EDIT children there and we
  // came up underneath them. SWP_NOACTIVATE preserves focus on the
  // app being typed into.
  SetWindowPos(child_hwnd_, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  LogLine("Select: SetWindowPos HWND_TOP done");

  // Probe: where are we, are we visible, what does the framework's
  // SIP-frame parent look like? Answers the "WM_PAINT fires but nothing
  // renders" mystery without another round-trip to the device.
  {
    RECT rc_parent_win = {0,0,0,0}, rc_parent_cli = {0,0,0,0};
    RECT rc_child_win = {0,0,0,0}, rc_child_cli = {0,0,0,0};
    GetWindowRect(panel_hwnd_, &rc_parent_win);
    GetClientRect(panel_hwnd_, &rc_parent_cli);
    GetWindowRect(child_hwnd_, &rc_child_win);
    GetClientRect(child_hwnd_, &rc_child_cli);
    LogRect("Select probe: parent GetWindowRect (screen) =", rc_parent_win);
    LogRect("Select probe: parent GetClientRect          =", rc_parent_cli);
    LogRect("Select probe: child  GetWindowRect (screen) =", rc_child_win);
    LogRect("Select probe: child  GetClientRect          =", rc_child_cli);
    LogLineInt("Select probe: IsWindowVisible(parent) = ", IsWindowVisible(panel_hwnd_) ? 1 : 0);
    LogLineInt("Select probe: IsWindowVisible(child)  = ", IsWindowVisible(child_hwnd_) ? 1 : 0);
    LogLineHex("Select probe: GetWindowLong(child,GWL_STYLE)   = ",
               static_cast<unsigned int>(GetWindowLongW(child_hwnd_, GWL_STYLE)));
    LogLineHex("Select probe: GetWindowLong(child,GWL_EXSTYLE) = ",
               static_cast<unsigned int>(GetWindowLongW(child_hwnd_, GWL_EXSTYLE)));
    LogLinePtr("Select probe: GetParent(child) = ", GetParent(child_hwnd_));
  }

  // Layout uses a sensible default; ReceiveSipInfo will refine it.
  RECT rc;
  GetClientRect(panel_hwnd_, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  if (w <= 0) w = 240;
  if (h <= 0) h = 80;
  RecomputeLayout(&panel_, w, h);
  LogLine("Select returning S_OK");
  return S_OK;
}

STDMETHODIMP RimeInputMethod::Deselect() {
  LogLine("Deselect entered");
  if (child_hwnd_) {
    UnregisterPanel(child_hwnd_);
    DestroyWindow(child_hwnd_);
    child_hwnd_ = NULL;
  }
  if (panel_.session) {
    RimeDestroySession(panel_.session);
    panel_.session = 0;
  }
  panel_hwnd_ = NULL;
  visible_ = false;
  return S_OK;
}

STDMETHODIMP RimeInputMethod::Showing() {
  // Dvorak SIP sample returns NOERROR with no work here. The framework
  // manages our parent's visibility; our child inherits visibility from
  // the parent. Painting fires naturally via WM_PAINT.
  LogLine("Showing entered");
  visible_ = true;
  return S_OK;
}

STDMETHODIMP RimeInputMethod::Hiding() {
  LogLine("Hiding entered");
  visible_ = false;
  // Push any pending commit before vanishing.
  std::string drained = DrainCommit(panel_.session);
  if (!drained.empty()) SendCommitText(drained.c_str());
  return S_OK;
}

STDMETHODIMP RimeInputMethod::GetInfo(IMINFO* pimi) {
  LogLine("GetInfo entered");
  if (!pimi) return E_POINTER;
  ZeroMemory(pimi, sizeof(*pimi));
  pimi->cbSize = sizeof(*pimi);
  pimi->hImageNarrow = NULL;
  pimi->hImageWide = NULL;
  pimi->iNarrow = 0;
  pimi->iWide = 0;
  // Dvorak sample sets SIPF_DOCKED here. With fdwFlags=0 the framework
  // was treating our SIP as not-yet-dockable and never positioned the
  // panel onto the screen properly, even though Showing() fired.
  pimi->fdwFlags = SIPF_DOCKED;
  pimi->rcSipRect.left = 0;
  pimi->rcSipRect.top = 0;
  pimi->rcSipRect.right = 240;
  pimi->rcSipRect.bottom = 80;
  return S_OK;
}

STDMETHODIMP RimeInputMethod::ReceiveSipInfo(SIPINFO* psi) {
  LogLine("ReceiveSipInfo entered");
  if (!psi) return E_POINTER;
  int w = psi->rcSipRect.right - psi->rcSipRect.left;
  int h = psi->rcSipRect.bottom - psi->rcSipRect.top;
  LogLineInt("ReceiveSipInfo: w=", w);
  LogLineInt("ReceiveSipInfo: h=", h);
  if (w > 0 && h > 0 && child_hwnd_) {
    // Mirror Dvorak: MoveWindow(child, 0, 0, w, h, FALSE). Position
    // is parent-relative so always (0, 0); the framework controls
    // where the parent itself sits on screen.
    MoveWindow(child_hwnd_, 0, 0, w, h, FALSE);
    RecomputeLayout(&panel_, w, h);
    InvalidateRect(child_hwnd_, NULL, TRUE);

    // Probe: did MoveWindow actually take effect, and where are we
    // sitting in screen coords after the framework has positioned the
    // parent SIP frame? If the parent is at y > screen_height we'd
    // paint correctly but off-screen.
    RECT rc_self = {0,0,0,0}, rc_parent = {0,0,0,0};
    GetWindowRect(child_hwnd_, &rc_self);
    GetWindowRect(panel_hwnd_, &rc_parent);
    LogRect("ReceiveSipInfo probe: child rect (screen)  =", rc_self);
    LogRect("ReceiveSipInfo probe: parent rect (screen) =", rc_parent);
    LogRect("ReceiveSipInfo probe: psi->rcSipRect       =", psi->rcSipRect);
    LogRect("ReceiveSipInfo probe: psi->rcVisibleDesktop=", psi->rcVisibleDesktop);
    LogLineInt("ReceiveSipInfo probe: IsWindowVisible(child) = ",
               IsWindowVisible(child_hwnd_) ? 1 : 0);
    LogLineInt("ReceiveSipInfo probe: IsWindowVisible(parent)= ",
               IsWindowVisible(panel_hwnd_) ? 1 : 0);
  }
  return S_OK;
}

STDMETHODIMP RimeInputMethod::RegisterCallback(IIMCallback* callback) {
  LogLinePtr("RegisterCallback entered, callback=", callback);
  if (callback_) { callback_->Release(); callback_ = NULL; }
  if (callback) {
    callback_ = callback;
    callback_->AddRef();
  }
  return S_OK;
}

STDMETHODIMP RimeInputMethod::GetImData(DWORD, void*) {
  // No persistent state to round-trip yet (user dict / options come
  // later). Return E_NOTIMPL per the SIP convention -- the shell
  // tolerates this for IMEs with no persisted blob.
  return E_NOTIMPL;
}

STDMETHODIMP RimeInputMethod::SetImData(DWORD, void*) {
  return E_NOTIMPL;
}

STDMETHODIMP RimeInputMethod::UserOptionsDlg(HWND) {
  // No options UI yet. Returning S_OK with no dialog is acceptable;
  // the Today screen / settings will just not show an "options" button.
  return S_OK;
}

// ----------------------------------------------------------------------
// IInputMethod2
// ----------------------------------------------------------------------

STDMETHODIMP RimeInputMethod::SetIMMActiveContext(HWND, BOOL bOpen,
                                                  DWORD, DWORD, DWORD) {
  // We don't manage an IMM (Asian IME) HIMC; for SIP-only mode this
  // is informational. Could be used later to clear composition on focus
  // loss; for MVP, no-op.
  (void)bOpen;
  return S_OK;
}

STDMETHODIMP RimeInputMethod::RegisterCallback2(IIMCallback2*) {
  // We use the simpler IIMCallback path via RegisterCallback. The Ex/2
  // callback adds richer event support that we don't need yet.
  return S_OK;
}

// ----------------------------------------------------------------------
// Output back to the focused app via IIMCallback.
// ----------------------------------------------------------------------

void RimeInputMethod::SendCommitText(const char* utf8) {
  if (!callback_ || !utf8 || !*utf8) return;
  std::wstring w = Utf8ToUtf16(utf8);
  if (w.empty()) return;
  BSTR bs = SysAllocStringLen(w.c_str(), static_cast<UINT>(w.size()));
  if (!bs) return;
  callback_->SendString(bs, static_cast<DWORD>(w.size()));
  SysFreeString(bs);
}

// ----------------------------------------------------------------------
// Subclassed window proc -- the meat of the UI.
// ----------------------------------------------------------------------

LRESULT CALLBACK RimeInputMethod::PanelWndProc(HWND hwnd, UINT msg,
                                               WPARAM wp, LPARAM lp) {
  // Log a hand-picked subset of messages relevant to visibility /
  // painting. We can't log every WM_* (would flood the log with mouse
  // moves, timers, etc.) but the ones below tell us whether the SIP
  // framework is actually trying to paint or show our window.
  switch (msg) {
    case WM_PAINT:        LogLine("PanelWndProc: WM_PAINT");        break;
    case WM_ERASEBKGND:   LogLine("PanelWndProc: WM_ERASEBKGND");   break;
    case WM_SHOWWINDOW:   LogLineInt("PanelWndProc: WM_SHOWWINDOW wp=", static_cast<int>(wp)); break;
    case WM_SIZE:         LogLineInt("PanelWndProc: WM_SIZE w=", LOWORD(lp)); break;
    case WM_WINDOWPOSCHANGED: LogLine("PanelWndProc: WM_WINDOWPOSCHANGED"); break;
    case WM_CREATE:       LogLine("PanelWndProc: WM_CREATE");       break;
    case WM_DESTROY:      LogLine("PanelWndProc: WM_DESTROY");      break;
  }

  RimeInputMethod* self = LookupPanel(hwnd);
  if (!self) {
    // No registered instance yet (WM_CREATE / pre-RegisterPanel) or
    // already torn down (post-Deselect). Just defer to the default
    // class wndproc.
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (hdc) {
        RECT cr;
        GetClientRect(hwnd, &cr);

        // Diagnostic probe: where are we ACTUALLY on screen at paint
        // time? If WM_PAINT logs "painted" but the user sees nothing,
        // we want to know whether the issue is "we're painting into
        // the void off-screen" vs "we're correctly positioned but the
        // composition pipeline drops our output". One sample per
        // WM_PAINT is fine -- there are usually only a few per show.
        {
          RECT rc_win = {0,0,0,0};
          GetWindowRect(hwnd, &rc_win);
          RECT rc_parent_win = {0,0,0,0};
          HWND parent = GetParent(hwnd);
          if (parent) GetWindowRect(parent, &rc_parent_win);
          LogRect("WM_PAINT probe: GetWindowRect(self) =", rc_win);
          LogRect("WM_PAINT probe: GetWindowRect(parent)=", rc_parent_win);
          LogRect("WM_PAINT probe: ps.rcPaint           =", ps.rcPaint);
          LogRect("WM_PAINT probe: GetClientRect(self)  =", cr);
          LogLineInt("WM_PAINT probe: IsWindowVisible(self)   = ", IsWindowVisible(hwnd) ? 1 : 0);
          LogLineInt("WM_PAINT probe: IsWindowVisible(parent) = ",
                     (parent && IsWindowVisible(parent)) ? 1 : 0);
        }

        // STRONG visibility test: paint the ENTIRE client area solid
        // red. If we can see red on-device, paint reaches the screen
        // and the keyboard layout / content is the next thing to fix.
        // If we still see nothing, the rendering surface itself isn't
        // making it to the framebuffer (composition / z-order /
        // off-screen parent).
        HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(hdc, &cr, red);
        DeleteObject(red);

        // Inset a green frame so we can also tell at a glance whether
        // edge clipping is eating part of our output.
        HBRUSH green = CreateSolidBrush(RGB(0, 200, 0));
        RECT top    = { cr.left, cr.top, cr.right, cr.top + 4 };
        RECT bottom = { cr.left, cr.bottom - 4, cr.right, cr.bottom };
        RECT left   = { cr.left, cr.top, cr.left + 4, cr.bottom };
        RECT right  = { cr.right - 4, cr.top, cr.right, cr.bottom };
        FillRect(hdc, &top, green);
        FillRect(hdc, &bottom, green);
        FillRect(hdc, &left, green);
        FillRect(hdc, &right, green);
        DeleteObject(green);

        PaintPanel(hdc, &self->panel_);
        EndPaint(hwnd, &ps);
        LogLine("PanelWndProc: WM_PAINT painted");
      } else {
        LogLine("PanelWndProc: WM_PAINT BeginPaint returned NULL");
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      int x = LOWORD(lp);
      int y = HIWORD(lp);
      std::string commit_text;
      HitResult hr = HandleTap(&self->panel_, x, y, &commit_text);
      if (!commit_text.empty()) {
        self->SendCommitText(commit_text.c_str());
      }
      // Refresh + redraw on any successful tap.
      if (hr != kHitNothing) {
        RefreshFromRime(&self->panel_);
        InvalidateRect(hwnd, NULL, TRUE);
      }
      return 0;
    }
    case WM_SIZE: {
      int w = LOWORD(lp);
      int h = HIWORD(lp);
      RecomputeLayout(&self->panel_, w, h);
      InvalidateRect(hwnd, NULL, TRUE);
      break;
    }
    case WM_ERASEBKGND:
      return 1;  // we paint full background in WM_PAINT
  }
  // No subclass chain in this design (we registered our own class).
  // Defer everything else to the default window procedure.
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace wmrime
