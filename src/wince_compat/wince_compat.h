//
// wince_compat/wince_compat.h -- umbrella include.
//
// Convenience header that pulls in every shim. Most clients (e.g. our rewrite
// of librime's common.h) include this once and then `using wince::foo;` what
// they need into the rime namespace.
//
// Order matters: function.h has no internal deps; signal.h depends on
// function.h and shared_ptr.h; path.h depends on utf.h.
//
#ifndef WINCE_COMPAT_H_
#define WINCE_COMPAT_H_

#include "utf.h"
#include "mutex.h"
#include "shared_ptr.h"
#include "function.h"
#include "signal.h"
#include "path.h"
#include "regex.h"

#endif  // WINCE_COMPAT_H_
