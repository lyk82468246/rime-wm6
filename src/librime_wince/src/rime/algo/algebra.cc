//
// rime/algo/algebra.cc -- WinCE-port mirror of upstream algebra.cc.
//
// Changes vs. upstream:
//   * C++11 range-for `for (const T& x : container)` -> classic iterator
//     loops. The four occurrences all loop over vector<Spelling> /
//     vector<an<Calculation>> / Script::value_type pairs.
//   * `auto e = std::find(...)` -> explicit `vector<Spelling>::iterator`.
//   * `catch (boost::regex_error&)` -> `catch (wince::regex_error&)`. We
//     also remove the redundant `<std::runtime_error&>` catches: our
//     regex engine never throws runtime_error from Apply, and the upstream
//     wrapping was for boost::regex iterator/replace edge cases that don't
//     apply to our Pike VM.
//   * Two CJK comments translated to ASCII.
//   * Dropped `<algorithm>` -- still need std::find, kept the include.
//   * `std::ofstream out(file_path.c_str())` -- our wince::path::c_str()
//     returns the UTF-8 char buffer, which MSVC 9.0's std::ofstream
//     accepts directly (no wide-string overload available pre-VS2010).
//     Filenames with non-ASCII bytes will fail on WinCE; the schema dump
//     path is developer-only so this is acceptable for MVP.
//
#include <algorithm>
#include <fstream>
#include <rime/algo/algebra.h>
#include <rime/algo/calculus.h>
// wince::regex_error is reachable via the rime/common.h -> wince_compat.h
// -> regex.h chain that calculus.h already pulls in.

namespace rime {

bool Script::AddSyllable(const string& syllable) {
  if (find(syllable) != end())
    return false;
  Spelling spelling(syllable);
  (*this)[syllable].push_back(spelling);
  return true;
}

void Script::Merge(const string& s,
                   const SpellingProperties& sp,
                   const vector<Spelling>& v) {
  vector<Spelling>& m((*this)[s]);
  for (vector<Spelling>::const_iterator it = v.begin(); it != v.end(); ++it) {
    Spelling y(*it);
    // Stack the rule's properties on top of the inherited ones.
    y.properties.Compose(sp);
    vector<Spelling>::iterator e = std::find(m.begin(), m.end(), *it);
    if (e == m.end()) {
      m.push_back(y);
    } else {
      // Merge attributes when an identical spelling already exists.
      e->properties.Update(y.properties);
    }
  }
}

void Script::Dump(const path& file_path) const {
  std::ofstream out(file_path.c_str());
  for (Script::const_iterator it = begin(); it != end(); ++it) {
    bool first = true;
    const vector<Spelling>& spellings = it->second;
    for (vector<Spelling>::const_iterator s = spellings.begin();
         s != spellings.end(); ++s) {
      out << (first ? it->first : "") << '\t' << s->str << '\t'
          << "-ac?!"[s->properties.type] << '\t' << s->properties.credibility
          << '\t' << s->properties.tips << std::endl;
      first = false;
    }
  }
  out.close();
}

bool Projection::Load(an<ConfigList> settings) {
  if (!settings)
    return false;
  calculation_.clear();
  Calculus calc;
  bool success = true;
  for (size_t i = 0; i < settings->size(); ++i) {
    an<ConfigValue> v(settings->GetValueAt(i));
    if (!v) {
      LOG(ERROR) << "Error loading formula #" << (i + 1) << ".";
      success = false;
      break;
    }
    const string& formula(v->str());
    an<Calculation> x;
    try {
      x.reset(calc.Parse(formula));
    } catch (const wince::regex_error& e) {
      LOG(ERROR) << "Error parsing formula '" << formula << "': " << e.what();
    }
    if (!x) {
      LOG(ERROR) << "Error loading spelling algebra definition #" << (i + 1)
                 << ": '" << formula << "'.";
      success = false;
      break;
    }
    calculation_.push_back(x);
  }
  if (!success) {
    calculation_.clear();
  }
  return success;
}

bool Projection::Apply(string* value) {
  if (!value || value->empty())
    return false;
  bool modified = false;
  Spelling s(*value);
  for (vector<of<Calculation> >::iterator it = calculation_.begin();
       it != calculation_.end(); ++it) {
    if ((*it)->Apply(&s))
      modified = true;
  }
  if (modified)
    value->assign(s.str);
  return modified;
}

bool Projection::Apply(Script* value) {
  if (!value || value->empty())
    return false;
  bool modified = false;
  for (vector<of<Calculation> >::iterator it = calculation_.begin();
       it != calculation_.end(); ++it) {
    of<Calculation>& x = *it;
    Script temp;
    for (Script::iterator vit = value->begin(); vit != value->end(); ++vit) {
      const string& key = vit->first;
      const vector<Spelling>& spellings = vit->second;
      Spelling s(key);
      bool applied = x->Apply(&s);
      if (applied) {
        modified = true;
        if (!x->deletion()) {
          temp.Merge(key, SpellingProperties(), spellings);
        }
        if (x->addition() && !s.str.empty()) {
          temp.Merge(s.str, s.properties, spellings);
        }
      } else {
        temp.Merge(key, SpellingProperties(), spellings);
      }
    }
    value->swap(temp);
  }
  return modified;
}

}  // namespace rime
