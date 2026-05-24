//
// wmrime_sip/class_factory.cc -- IClassFactory implementation.
//
#include "class_factory.h"
#include "sip_globals.h"
#include "rime_input_method.h"
#include "log.h"

#include <new>

namespace wmrime {

RimeClassFactory::RimeClassFactory() : ref_count_(1) {
  InterlockedIncrement(&g_object_count);
  LogLine("ClassFactory: ctor");
}

RimeClassFactory::~RimeClassFactory() {
  InterlockedDecrement(&g_object_count);
  LogLine("ClassFactory: dtor");
}

STDMETHODIMP RimeClassFactory::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
    *ppv = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
  }
  LogLine("ClassFactory: QI rejected (not IUnknown/IClassFactory)");
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) RimeClassFactory::AddRef() {
  return InterlockedIncrement(&ref_count_);
}

STDMETHODIMP_(ULONG) RimeClassFactory::Release() {
  LONG c = InterlockedDecrement(&ref_count_);
  if (c == 0) delete this;
  return static_cast<ULONG>(c);
}

STDMETHODIMP RimeClassFactory::CreateInstance(IUnknown* outer,
                                              REFIID riid, void** ppv) {
  LogLine("ClassFactory::CreateInstance entered");
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  // SIP framework never asks us to aggregate.
  if (outer) {
    LogLine("ClassFactory::CreateInstance rejecting aggregation");
    return CLASS_E_NOAGGREGATION;
  }
  RimeInputMethod* obj = new (std::nothrow) RimeInputMethod();
  if (!obj) {
    LogLine("ClassFactory::CreateInstance OOM allocating RimeInputMethod");
    return E_OUTOFMEMORY;
  }
  HRESULT hr = obj->QueryInterface(riid, ppv);
  obj->Release();
  LogLineHex("ClassFactory::CreateInstance returning hr=", static_cast<unsigned int>(hr));
  return hr;
}

STDMETHODIMP RimeClassFactory::LockServer(BOOL lock) {
  if (lock) InterlockedIncrement(&g_lock_count);
  else      InterlockedDecrement(&g_lock_count);
  return S_OK;
}

}  // namespace wmrime
