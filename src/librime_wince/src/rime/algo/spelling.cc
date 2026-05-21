//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2012-01-17 GONG Chen <chen.sst@gmail.com>
// 2026-05    WinCE port: comments translated to English to satisfy the
//            ASCII-only rule for source files (C4819 / CP936 mitigation).
//
// No code changes from upstream src/librime/src/rime/algo/spelling.cc.
//
#include "spelling.h"

namespace rime {

void SpellingProperties::Compose(const SpellingProperties& delta) {
  // Take the fuzziest type (highest enum value wins).
  if (delta.type > type) {
    type = delta.type;
  }
  // Accumulate credibility.
  credibility += delta.credibility;
  // Sticky correction flag: once set, stays set.
  if (delta.is_correction) {
    is_correction = true;
  }

  if (!delta.tips.empty()) {
    tips = delta.tips;
  }
}

void SpellingProperties::Update(const SpellingProperties& other) {
  // Same type: keep correction flag only if BOTH sides agreed it's a correction.
  if (type == other.type) {
    is_correction = is_correction && other.is_correction;
  }
  // Adopt the less fuzzy type (lower enum value wins).
  else if (other.type < type) {
    type = other.type;
    // Correction flag follows the newly-adopted type.
    is_correction = other.is_correction;
  }
  // Retain the higher credibility.
  if (other.credibility > credibility) {
    credibility = other.credibility;
  }
  // The original tip referred to one of the merged sources; it may not apply
  // to the combined spelling, so we drop it.
  tips.clear();
}

}  // namespace rime
