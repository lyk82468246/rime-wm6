//
// wince_compat/function.h -- std::function replacement for 0..3-arg callables.
//
// librime declares signal handlers with up to two arguments (see signal.h),
// plus Service::NotificationHandler which takes three (session_id, type,
// value). So we provide four arities. Add more if a future port turns up
// callsites needing them.
//
// Implementation: classic type-erased callable. Each function instance holds
// a heap-allocated polymorphic wrapper around the original functor /
// function-pointer / member-binder; copy clones the wrapper.
//
// Limitations vs. std::function:
//   - Assignment from a callable always allocates. No SBO.
//   - No `target<T>()` / `target_type()` (we don't need RTTI introspection).
//   - operator() on an empty function dereferences a null pointer (undefined
//     behaviour). std::function would throw bad_function_call; we don't,
//     because rime never invokes empty signal slots -- they're always assigned
//     before being called.
//
#ifndef WINCE_COMPAT_FUNCTION_H_
#define WINCE_COMPAT_FUNCTION_H_

#include <cstddef>  // NULL

namespace wince {

template <class Signature> class function;

// ---------------------------------------------------------------------------
// function<R()>
// ---------------------------------------------------------------------------
template <class R>
class function<R()> {
  struct impl_base {
    virtual ~impl_base() {}
    virtual R call() = 0;
    virtual impl_base* clone() const = 0;
  };
  template <class F>
  struct impl : impl_base {
    F f;
    impl(const F& f_) : f(f_) {}
    virtual R call() { return f(); }
    virtual impl_base* clone() const { return new impl(f); }
  };

 public:
  function() : impl_(NULL) {}

  template <class F>
  function(const F& f) : impl_(new impl<F>(f)) {}

  function(const function& o) : impl_(o.impl_ ? o.impl_->clone() : NULL) {}
  ~function() { delete impl_; }

  function& operator=(const function& o) {
    function(o).swap(*this);
    return *this;
  }
  void swap(function& o) {
    impl_base* t = impl_; impl_ = o.impl_; o.impl_ = t;
  }

  R operator()() const { return impl_->call(); }

  typedef impl_base* function::*unspecified_bool_type;
  operator unspecified_bool_type() const {
    return impl_ ? &function::impl_ : NULL;
  }

 private:
  impl_base* impl_;
};

// ---------------------------------------------------------------------------
// function<R(A1)>
// ---------------------------------------------------------------------------
template <class R, class A1>
class function<R(A1)> {
  struct impl_base {
    virtual ~impl_base() {}
    virtual R call(A1) = 0;
    virtual impl_base* clone() const = 0;
  };
  template <class F>
  struct impl : impl_base {
    F f;
    impl(const F& f_) : f(f_) {}
    virtual R call(A1 a1) { return f(a1); }
    virtual impl_base* clone() const { return new impl(f); }
  };

 public:
  function() : impl_(NULL) {}
  template <class F>
  function(const F& f) : impl_(new impl<F>(f)) {}
  function(const function& o) : impl_(o.impl_ ? o.impl_->clone() : NULL) {}
  ~function() { delete impl_; }

  function& operator=(const function& o) {
    function(o).swap(*this);
    return *this;
  }
  void swap(function& o) {
    impl_base* t = impl_; impl_ = o.impl_; o.impl_ = t;
  }

  R operator()(A1 a1) const { return impl_->call(a1); }

  typedef impl_base* function::*unspecified_bool_type;
  operator unspecified_bool_type() const {
    return impl_ ? &function::impl_ : NULL;
  }

 private:
  impl_base* impl_;
};

// ---------------------------------------------------------------------------
// function<R(A1, A2)>
// ---------------------------------------------------------------------------
template <class R, class A1, class A2>
class function<R(A1, A2)> {
  struct impl_base {
    virtual ~impl_base() {}
    virtual R call(A1, A2) = 0;
    virtual impl_base* clone() const = 0;
  };
  template <class F>
  struct impl : impl_base {
    F f;
    impl(const F& f_) : f(f_) {}
    virtual R call(A1 a1, A2 a2) { return f(a1, a2); }
    virtual impl_base* clone() const { return new impl(f); }
  };

 public:
  function() : impl_(NULL) {}
  template <class F>
  function(const F& f) : impl_(new impl<F>(f)) {}
  function(const function& o) : impl_(o.impl_ ? o.impl_->clone() : NULL) {}
  ~function() { delete impl_; }

  function& operator=(const function& o) {
    function(o).swap(*this);
    return *this;
  }
  void swap(function& o) {
    impl_base* t = impl_; impl_ = o.impl_; o.impl_ = t;
  }

  R operator()(A1 a1, A2 a2) const { return impl_->call(a1, a2); }

  typedef impl_base* function::*unspecified_bool_type;
  operator unspecified_bool_type() const {
    return impl_ ? &function::impl_ : NULL;
  }

 private:
  impl_base* impl_;
};

// ---------------------------------------------------------------------------
// function<R(A1, A2, A3)>
// ---------------------------------------------------------------------------
template <class R, class A1, class A2, class A3>
class function<R(A1, A2, A3)> {
  struct impl_base {
    virtual ~impl_base() {}
    virtual R call(A1, A2, A3) = 0;
    virtual impl_base* clone() const = 0;
  };
  template <class F>
  struct impl : impl_base {
    F f;
    impl(const F& f_) : f(f_) {}
    virtual R call(A1 a1, A2 a2, A3 a3) { return f(a1, a2, a3); }
    virtual impl_base* clone() const { return new impl(f); }
  };

 public:
  function() : impl_(NULL) {}
  template <class F>
  function(const F& f) : impl_(new impl<F>(f)) {}
  function(const function& o) : impl_(o.impl_ ? o.impl_->clone() : NULL) {}
  ~function() { delete impl_; }

  function& operator=(const function& o) {
    function(o).swap(*this);
    return *this;
  }
  void swap(function& o) {
    impl_base* t = impl_; impl_ = o.impl_; o.impl_ = t;
  }

  R operator()(A1 a1, A2 a2, A3 a3) const { return impl_->call(a1, a2, a3); }

  typedef impl_base* function::*unspecified_bool_type;
  operator unspecified_bool_type() const {
    return impl_ ? &function::impl_ : NULL;
  }

 private:
  impl_base* impl_;
};

}  // namespace wince

#endif  // WINCE_COMPAT_FUNCTION_H_
