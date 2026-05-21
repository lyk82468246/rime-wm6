//
// wmrime_sip/class_factory.h -- IClassFactory for the WMRimeSIP CLSID.
//
// The shell's COM activator calls DllGetClassObject, gets one of
// these, then calls CreateInstance to obtain an IInputMethod /
// IInputMethod2. Standard COM boilerplate.
//
#ifndef WMRIME_SIP_CLASS_FACTORY_H_
#define WMRIME_SIP_CLASS_FACTORY_H_

#include <objbase.h>

namespace wmrime {

class RimeClassFactory : public IClassFactory {
 public:
  RimeClassFactory();
  virtual ~RimeClassFactory();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IClassFactory
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv);
  STDMETHODIMP LockServer(BOOL lock);

 private:
  LONG ref_count_;
};

}  // namespace wmrime

#endif  // WMRIME_SIP_CLASS_FACTORY_H_
