//
// rime/composition.cc -- WinCE-port mirror of upstream composition.cc.
//
// Changes vs. upstream:
//   * <boost/algorithm/string.hpp> and <boost/range/adaptor/reversed.hpp>
//     dropped. boost::erase_first_copy(s, "\t") replaced with a hand-rolled
//     `EraseFirst(s, '\t')`; boost::adaptors::reverse swapped for a plain
//     reverse_iterator loop.
//   * `auto cand = ...` -> explicit an<Candidate>.
//   * Range-for `for (const Segment& seg : *this)` -> classic iterator
//     loops at every occurrence (4 in this file).
//
#include <rime/candidate.h>
#include <rime/composition.h>
#include <rime/menu.h>

namespace rime {

namespace {

// Hand-rolled replacement for boost::erase_first_copy(s, "\t"). We only
// ever pass a single char here, so the simpler char-based signature is
// enough.
string EraseFirst(const string& s, char ch) {
  string r = s;
  size_t p = r.find(ch);
  if (p != string::npos) r.erase(p, 1);
  return r;
}

}  // namespace

bool Composition::HasFinishedComposition() const {
  if (empty())
    return false;
  size_t k = size() - 1;
  if (k > 0 && at(k).start == at(k).end)
    --k;
  return at(k).status >= Segment::kSelected;
}

Preedit Composition::GetPreedit(const string& full_input,
                                size_t caret_pos,
                                const string& caret) const {
  Preedit preedit;
  preedit.caret_pos = string::npos;
  size_t start = 0;
  size_t end = 0;
  for (size_t i = 0; i < size(); ++i) {
    start = end;
    if (caret_pos == start) {
      preedit.caret_pos = preedit.text.length();
    }
    an<Candidate> cand = at(i).GetSelectedCandidate();
    if (i < size() - 1) {  // converted
      if (cand) {
        end = cand->end();
        preedit.text += cand->text();
      } else {  // raw input
        end = at(i).end;
        if (!at(i).HasTag("phony")) {
          preedit.text += input_.substr(start, end - start);
        }
      }
    } else {  // highlighted
      preedit.sel_start = preedit.text.length();
      preedit.sel_end = string::npos;
      if (cand && !cand->preedit().empty()) {
        end = cand->end();
        size_t caret_placeholder = cand->preedit().find('\t');
        if (caret_placeholder != string::npos) {
          preedit.text += cand->preedit().substr(0, caret_placeholder);
          // the part after caret is considered prompt string,
          // show it only when the caret is at the end of input.
          if (caret_pos == end && end == full_input.length()) {
            preedit.sel_end = preedit.sel_start + caret_placeholder;
            preedit.caret_pos = preedit.sel_end;
            preedit.text += cand->preedit().substr(caret_placeholder + 1);
          }
        } else {
          preedit.text += cand->preedit();
        }
      } else {
        end = at(i).end;
        preedit.text += input_.substr(start, end - start);
      }
      if (preedit.sel_end == string::npos) {
        preedit.sel_end = preedit.text.length();
      }
    }
  }
  if (end < input_.length()) {
    preedit.text += input_.substr(end);
    end = input_.length();
  }
  if (preedit.caret_pos == string::npos) {
    preedit.caret_pos = preedit.text.length();
  }
  if (end < full_input.length()) {
    preedit.text += full_input.substr(end);
  }
  // insert soft cursor and prompt string.
  string prompt = caret + GetPrompt();
  if (!prompt.empty()) {
    preedit.text.insert(preedit.caret_pos, prompt);
    if (preedit.caret_pos < preedit.sel_start) {
      preedit.sel_start += prompt.length();
    }
    if (preedit.caret_pos < preedit.sel_end) {
      preedit.sel_end += prompt.length();
    }
  }
  return preedit;
}

string Composition::GetPrompt() const {
  return empty() ? string() : back().prompt;
}

string Composition::GetCommitText() const {
  string result;
  size_t end = 0;
  for (Composition::const_iterator it = begin(); it != this->end(); ++it) {
    const Segment& seg = *it;
    an<Candidate> cand = seg.GetSelectedCandidate();
    if (cand) {
      end = cand->end();
      result += cand->text();
    } else {
      end = seg.end;
      if (!seg.HasTag("phony")) {
        result += input_.substr(seg.start, seg.end - seg.start);
      }
    }
  }
  if (input_.length() > end) {
    result += input_.substr(end);
  }
  return result;
}

string Composition::GetScriptText(bool keep_selection) const {
  string result;
  size_t start = 0;
  size_t end = 0;
  for (Composition::const_iterator it = begin(); it != this->end(); ++it) {
    const Segment& seg = *it;
    an<Candidate> cand = seg.GetSelectedCandidate();
    start = end;
    end = cand ? cand->end() : seg.end;
    if (keep_selection && cand && !cand->text().empty() &&
        seg.status >= Segment::kSelected)
      result += cand->text();
    else if (cand && !cand->preedit().empty())
      result += EraseFirst(cand->preedit(), '\t');
    else if (!seg.HasTag("phony"))
      result += input_.substr(start, end - start);
  }
  if (input_.length() > end) {
    result += input_.substr(end);
  }
  return result;
}

string Composition::GetDebugText() const {
  string result;
  int i = 0;
  for (Composition::const_iterator it = begin(); it != end(); ++it) {
    const Segment& seg = *it;
    if (i++ > 0)
      result += "|";
    if (!seg.tags.empty()) {
      result += "{";
      int j = 0;
      for (set<string>::const_iterator tit = seg.tags.begin();
           tit != seg.tags.end(); ++tit) {
        if (j++ > 0)
          result += ",";
        result += *tit;
      }
      result += "}";
    }
    result += input_.substr(seg.start, seg.end - seg.start);
    an<Candidate> cand = seg.GetSelectedCandidate();
    if (cand) {
      result += "=>";
      result += cand->text();
    }
  }
  return result;
}

string Composition::GetTextBefore(size_t pos) const {
  if (empty())
    return string();
  // upstream uses boost::adaptors::reverse(*this); we walk reverse_iterator
  // directly, which avoids the boost dep without changing semantics.
  for (Composition::const_reverse_iterator it = rbegin();
       it != rend(); ++it) {
    const Segment& seg = *it;
    if (seg.end <= pos) {
      an<Candidate> cand = seg.GetSelectedCandidate();
      if (cand) {
        return cand->text();
      }
    }
  }
  return string();
}

}  // namespace rime
