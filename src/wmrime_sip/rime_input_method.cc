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

}  // namespace

RimeInputMethod::RimeInputMethod()
    : ref_count_(1),
      panel_hwnd_(NULL),
      orig_wndproc_(NULL),
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

  panel_hwnd_ = hwndSip;
  panel_.session = RimeCreateSession();
  LogLineInt("Select: RimeCreateSession returned id=", static_cast<int>(panel_.session));
  RegisterPanel(panel_hwnd_, this);
  orig_wndproc_ = reinterpret_cast<WNDPROC>(
      SetWindowLongW(panel_hwnd_, GWL_WNDPROC,
                     reinterpret_cast<LONG>(&RimeInputMethod::PanelWndProc)));
  LogLinePtr("Select: SetWindowLong replaced orig_wndproc_=", orig_wndproc_);

  RECT rc;
  GetClientRect(panel_hwnd_, &rc);
  LogLineInt("Select: client width=", rc.right - rc.left);
  LogLineInt("Select: client height=", rc.bottom - rc.top);
  RecomputeLayout(&panel_, rc.right - rc.left, rc.bottom - rc.top);
  LogLine("Select returning S_OK");
  return S_OK;
}

STDMETHODIMP RimeInputMethod::Deselect() {
  LogLine("Deselect entered");
  if (panel_hwnd_ && orig_wndproc_) {
    SetWindowLongW(panel_hwnd_, GWL_WNDPROC,
                   reinterpret_cast<LONG>(orig_wndproc_));
    UnregisterPanel(panel_hwnd_);
    orig_wndproc_ = NULL;
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
  LogLine("Showing entered");
  visible_ = true;
  if (panel_hwnd_) {
    RefreshFromRime(&panel_);
    InvalidateRect(panel_hwnd_, NULL, TRUE);
  }
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
  // Per MSDN IMINFO docs, fdwFlags here describes the SIP's current
  // characteristics. We set 0 (not SIPF_ON or SIPF_DOCKED -- those are
  // status flags managed by the framework, not advertised by GetInfo
  // before Select). The framework will tell us the docked rect via
  // ReceiveSipInfo right after Select.
  pimi->fdwFlags = 0;
  // Default rect: full SIP rect that the framework usually overrides
  // immediately. Set bottom-anchored values so even if ReceiveSipInfo
  // never fires we get a usable panel.
  pimi->rcSipRect.left = 0;
  pimi->rcSipRect.top = 0;
  pimi->rcSipRect.right = 240;
  pimi->rcSipRect.bottom = 80;
  LogLine("GetInfo returning S_OK");
  return S_OK;
}

STDMETHODIMP RimeInputMethod::ReceiveSipInfo(SIPINFO* psi) {
  LogLine("ReceiveSipInfo entered");
  if (!psi) return E_POINTER;
  int w = psi->rcSipRect.right - psi->rcSipRect.left;
  int h = psi->rcSipRect.bottom - psi->rcSipRect.top;
  LogLineInt("ReceiveSipInfo: w=", w);
  LogLineInt("ReceiveSipInfo: h=", h);
  if (w > 0 && h > 0) RecomputeLayout(&panel_, w, h);
  if (panel_hwnd_) InvalidateRect(panel_hwnd_, NULL, TRUE);
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
  RimeInputMethod* self = LookupPanel(hwnd);
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (hdc) {
        PaintPanel(hdc, &self->panel_);
        EndPaint(hwnd, &ps);
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
  return CallWindowProcW(self->orig_wndproc_, hwnd, msg, wp, lp);
}

}  // namespace wmrime
