//
// wmrime_sip/rime_input_method.h -- IInputMethod2 impl that owns the
// SIP panel HWND and routes user input through rime_api.
//
// Lifecycle:
//   1. Class factory creates a RimeInputMethod when the SIP framework
//      asks for our CLSID.
//   2. Framework calls Select(hwndSip), giving us a parent HWND. We
//      subclass its WNDPROC, create a Rime session, and own draw/key
//      handling until Deselect.
//   3. Showing / Hiding -- update visible flag; we redraw accordingly.
//   4. RegisterCallback gives us the IIMCallback we use to push
//      committed text into the focused field via SendString.
//   5. ReceiveSipInfo carries the panel geometry (we use rcSipRect
//      to lay out candidate strip + soft keyboard).
//
// All COM-visible state lives in this class; the UI / hit-test code
// lives in sip_window.cc as a set of free functions that operate on
// our PanelState struct.
//
#ifndef WMRIME_SIP_RIME_INPUT_METHOD_H_
#define WMRIME_SIP_RIME_INPUT_METHOD_H_

#include <objbase.h>
#include <sip.h>
#include <windows.h>

#include "sip_window.h"

namespace wmrime {

class RimeInputMethod : public IInputMethod2 {
 public:
  RimeInputMethod();
  virtual ~RimeInputMethod();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IInputMethod
  STDMETHODIMP Select(HWND hwndSip);
  STDMETHODIMP Deselect();
  STDMETHODIMP Showing();
  STDMETHODIMP Hiding();
  STDMETHODIMP GetInfo(IMINFO* pimi);
  STDMETHODIMP ReceiveSipInfo(SIPINFO* psi);
  STDMETHODIMP RegisterCallback(IIMCallback* callback);
  STDMETHODIMP GetImData(DWORD dwSize, void* pvImData);
  STDMETHODIMP SetImData(DWORD dwSize, void* pvImData);
  STDMETHODIMP UserOptionsDlg(HWND hwndParent);

  // IInputMethod2
  STDMETHODIMP SetIMMActiveContext(HWND hwnd, BOOL bOpen,
                                   DWORD dwConversion, DWORD dwSentence,
                                   DWORD hkl);
  STDMETHODIMP RegisterCallback2(IIMCallback2* callback);

  // Called from the subclassed window proc (sip_window.cc) when the
  // panel needs to commit text back to the focused application.
  void SendCommitText(const char* utf8);

 private:
  static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg,
                                       WPARAM wp, LPARAM lp);

  LONG ref_count_;
  HWND panel_hwnd_;        // SIP-frame HWND given by framework (parent)
  HWND child_hwnd_;        // our own WS_CHILD that we subclass + paint
  WNDPROC orig_wndproc_;   // child's original wndproc (for chaining)
  IIMCallback* callback_;
  PanelState panel_;
  bool visible_;
};

}  // namespace wmrime

#endif  // WMRIME_SIP_RIME_INPUT_METHOD_H_
