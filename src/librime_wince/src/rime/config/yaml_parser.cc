//
// rime/config/yaml_parser.cc -- minimal hand-written YAML parser.
//
// Approach: two-pass.
//   1. Preprocess input into a vector of `PhysLine` records: track 1-based
//      lineno, leading-space indent, content with leading indent and any
//      trailing comment stripped, and a few flags (blank / doc-open
//      "---" / doc-close "...").
//   2. Single-pass recursive descent over the line vector that always
//      maintains a single invariant:
//        on entry, pos_ points to the next line that may belong to the
//        construct being parsed (or end()); on exit, pos_ points past
//        every line the construct consumed.
//
// The parser owns pos_ advancement; helpers like splitMapKey /
// parseInlineValue work on a borrowed string in place and never touch
// pos_. parseValue(parent_indent) dispatches between parseMapping
// and parseSequence based on the first nested indented line's first
// non-space char.
//
// Error handling: write a "line N: <reason>" message into the optional
// error string and return null. We never throw -- libcxx exceptions on
// WinCE are doable but not worth the complexity for a config loader.
//
#include <rime/config/yaml_parser.h>

#include <cctype>
#include <cstdlib>
#include <sstream>

#include <rime/config/config_types.h>

namespace rime {
namespace yaml {

namespace {

struct PhysLine {
  size_t lineno;       // 1-based, for diagnostics
  int indent;          // count of leading spaces (>= 0)
  string content;      // post-strip, no leading indent, no trailing comment
  bool is_blank;       // empty content
  bool is_doc_open;    // "---" alone
  bool is_doc_close;   // "..." alone
};

// ------------------------------------------------------------------
// Comment-stripping: walk the line, honoring single/double quotes and
// flow brackets. A '#' that's preceded by whitespace OR sits at column
// 0, AND is not inside a quote/flow context, starts a comment.
// Returns the index of the first comment char (or s.size() if none).
// ------------------------------------------------------------------
size_t find_comment_start(const string& s) {
  bool in_squote = false;
  bool in_dquote = false;
  int flow_depth = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (in_squote) {
      // YAML doubles single quotes for escape: '' inside '...'.
      if (c == '\'' && i + 1 < s.size() && s[i + 1] == '\'') {
        ++i;
        continue;
      }
      if (c == '\'') in_squote = false;
      continue;
    }
    if (in_dquote) {
      if (c == '\\' && i + 1 < s.size()) {
        ++i;
        continue;
      }
      if (c == '"') in_dquote = false;
      continue;
    }
    if (c == '\'') { in_squote = true; continue; }
    if (c == '"')  { in_dquote = true; continue; }
    if (c == '[' || c == '{') { ++flow_depth; continue; }
    if (c == ']' || c == '}') { if (flow_depth) --flow_depth; continue; }
    if (c == '#' && flow_depth == 0) {
      // Must be at start of line or preceded by whitespace.
      if (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')
        return i;
    }
  }
  return s.size();
}

string rstrip(const string& s) {
  size_t end = s.size();
  while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                     s[end - 1] == '\r'))
    --end;
  return s.substr(0, end);
}

string strip(const string& s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
    ++start;
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                         s[end - 1] == '\r'))
    --end;
  return s.substr(start, end - start);
}

bool starts_with(const string& s, const char* prefix) {
  size_t i = 0;
  while (prefix[i]) {
    if (i >= s.size() || s[i] != prefix[i]) return false;
    ++i;
  }
  return true;
}

// ------------------------------------------------------------------
// Preprocess: split into PhysLines, strip indent + inline comments,
// detect doc-open / doc-close. Returns false if a hard error (e.g.
// tab in indent) was hit.
// ------------------------------------------------------------------
bool preprocess(const string& text, vector<PhysLine>* out, string* err) {
  size_t i = 0;
  size_t n = text.size();
  size_t lineno = 0;
  // Strip UTF-8 BOM at start if present.
  if (n >= 3 && (unsigned char)text[0] == 0xEF &&
      (unsigned char)text[1] == 0xBB &&
      (unsigned char)text[2] == 0xBF) {
    i = 3;
  }
  while (i <= n) {
    ++lineno;
    size_t line_start = i;
    while (i < n && text[i] != '\n') ++i;
    string raw = text.substr(line_start, i - line_start);
    if (i < n) ++i;  // skip the newline

    PhysLine pl;
    pl.lineno = lineno;
    pl.indent = 0;
    pl.is_blank = false;
    pl.is_doc_open = false;
    pl.is_doc_close = false;

    // Count leading spaces; reject leading tabs.
    size_t j = 0;
    while (j < raw.size() && raw[j] == ' ') ++j;
    if (j < raw.size() && raw[j] == '\t') {
      if (err) {
        std::ostringstream os;
        os << "line " << lineno << ": tabs not allowed in indentation";
        *err = os.str();
      }
      return false;
    }
    pl.indent = static_cast<int>(j);

    // Strip inline comment + trailing whitespace.
    string after_indent = raw.substr(j);
    size_t cstart = find_comment_start(after_indent);
    string content = rstrip(after_indent.substr(0, cstart));

    if (content.empty()) {
      pl.is_blank = true;
    } else if (pl.indent == 0 && content == "---") {
      pl.is_doc_open = true;
    } else if (pl.indent == 0 && content == "...") {
      pl.is_doc_close = true;
    }
    pl.content = content;
    out->push_back(pl);

    if (i > n) break;  // last iteration
  }
  return true;
}

// ------------------------------------------------------------------
// Scan for a top-level ": " (or ":" at end-of-string) outside quotes /
// flow brackets. Returns the index of the ':' or string::npos.
// ------------------------------------------------------------------
size_t find_map_colon(const string& s) {
  bool in_squote = false;
  bool in_dquote = false;
  int flow_depth = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (in_squote) {
      if (c == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
      if (c == '\'') in_squote = false;
      continue;
    }
    if (in_dquote) {
      if (c == '\\' && i + 1 < s.size()) { ++i; continue; }
      if (c == '"') in_dquote = false;
      continue;
    }
    if (c == '\'') { in_squote = true; continue; }
    if (c == '"')  { in_dquote = true; continue; }
    if (c == '[' || c == '{') { ++flow_depth; continue; }
    if (c == ']' || c == '}') { if (flow_depth) --flow_depth; continue; }
    if (c == ':' && flow_depth == 0) {
      // Map colon: either followed by space, or at end of string.
      if (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t')
        return i;
    }
  }
  return string::npos;
}

bool looks_like_map_start(const string& s) {
  return find_map_colon(s) != string::npos;
}

// Unquote a key or scalar with quotes. Returns false if the quoting is
// malformed.
bool unquote_single(const string& s, string* out) {
  // s is whole token including the surrounding quotes.
  if (s.size() < 2 || s[0] != '\'' || s[s.size() - 1] != '\'') return false;
  string r;
  r.reserve(s.size() - 2);
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    if (s[i] == '\'' && i + 2 < s.size() && s[i + 1] == '\'') {
      r += '\'';
      ++i;
    } else if (s[i] == '\'') {
      return false;  // stray single quote inside
    } else {
      r += s[i];
    }
  }
  *out = r;
  return true;
}

bool unquote_double(const string& s, string* out) {
  if (s.size() < 2 || s[0] != '"' || s[s.size() - 1] != '"') return false;
  string r;
  r.reserve(s.size() - 2);
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    if (s[i] == '\\' && i + 2 < s.size()) {
      char e = s[i + 1];
      switch (e) {
        case 'n': r += '\n'; break;
        case 't': r += '\t'; break;
        case 'r': r += '\r'; break;
        case '"': r += '"'; break;
        case '\\': r += '\\'; break;
        case '/': r += '/'; break;
        case '0': r += '\0'; break;
        default: r += e; break;  // permissive: unknown escape -> literal
      }
      ++i;
    } else {
      r += s[i];
    }
  }
  *out = r;
  return true;
}

// Strip wrapping quotes if any, otherwise return s unchanged. Used
// for keys which are always treated as strings.
string unquote_key(const string& s) {
  if (s.size() >= 2 && s[0] == '\'' && s[s.size() - 1] == '\'') {
    string out;
    if (unquote_single(s, &out)) return out;
  }
  if (s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"') {
    string out;
    if (unquote_double(s, &out)) return out;
  }
  return s;
}

// ------------------------------------------------------------------
// Inline-value tokenizer. Reads from `s` starting at `pos`, advances
// pos past the value. Skips leading whitespace. Returns the parsed
// item.
//   * `[`  -> flow sequence (recursive)
//   * `{`  -> flow mapping (recursive)
//   * `'`  -> single-quoted scalar
//   * `"`  -> double-quoted scalar
//   * else -> plain scalar up to flow separator or comment
// ------------------------------------------------------------------

an<ConfigItem> parse_flow_value(const string& s, size_t* pos, string* err);

void skip_ws(const string& s, size_t* pos) {
  while (*pos < s.size() && (s[*pos] == ' ' || s[*pos] == '\t')) ++(*pos);
}

// Promote a plain scalar to int / double / bool / null based on shape.
// Always succeeds (worst case returns a string-valued ConfigValue).
an<ConfigValue> scalar_to_value(const string& raw) {
  // null
  if (raw == "~" || raw == "null" || raw == "Null" || raw == "NULL" ||
      raw.empty())
    return an<ConfigValue>();  // null-valued
  // booleans
  if (raw == "true" || raw == "True" || raw == "TRUE" ||
      raw == "yes" || raw == "Yes" || raw == "YES")
    return New<ConfigValue>(true);
  if (raw == "false" || raw == "False" || raw == "FALSE" ||
      raw == "no" || raw == "No" || raw == "NO")
    return New<ConfigValue>(false);
  // integer (decimal). Tolerate leading +/-.
  {
    size_t i = 0;
    bool ok = true;
    if (i < raw.size() && (raw[i] == '+' || raw[i] == '-')) ++i;
    if (i >= raw.size()) ok = false;
    size_t digit_start = i;
    while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9') ++i;
    if (ok && i > digit_start && i == raw.size()) {
      return New<ConfigValue>(std::atoi(raw.c_str()));
    }
  }
  // float
  {
    size_t i = 0;
    bool ok = true;
    bool saw_dot = false, saw_digit = false;
    if (i < raw.size() && (raw[i] == '+' || raw[i] == '-')) ++i;
    for (; i < raw.size(); ++i) {
      char c = raw[i];
      if (c >= '0' && c <= '9') { saw_digit = true; continue; }
      if (c == '.') {
        if (saw_dot) { ok = false; break; }
        saw_dot = true;
        continue;
      }
      ok = false; break;
    }
    if (ok && saw_dot && saw_digit) {
      return New<ConfigValue>(std::atof(raw.c_str()));
    }
  }
  // Plain string.
  return New<ConfigValue>(raw);
}

// Scan a quoted token starting at *pos; return the token's full text
// (including the surrounding quotes). On unterminated quote, returns
// "" and sets *ok = false.
string scan_quoted(const string& s, size_t* pos, bool* ok) {
  char q = s[*pos];
  size_t start = *pos;
  size_t i = start + 1;
  while (i < s.size()) {
    char c = s[i];
    if (q == '\'' && c == '\'' && i + 1 < s.size() && s[i + 1] == '\'') {
      i += 2;  // doubled single quote (escape)
      continue;
    }
    if (q == '\'' && c == '\'') { ++i; *pos = i; *ok = true; return s.substr(start, i - start); }
    if (q == '"' && c == '\\' && i + 1 < s.size()) { i += 2; continue; }
    if (q == '"' && c == '"') { ++i; *pos = i; *ok = true; return s.substr(start, i - start); }
    ++i;
  }
  *ok = false;
  return string();
}

// Scan a plain scalar starting at *pos in `s` until a flow separator
// (",", "]", "}") or end of string. Stops trailing whitespace. Does
// NOT cross a top-level ":" -- caller handles map-key detection
// before calling this.
string scan_plain(const string& s, size_t* pos, bool stop_on_colon) {
  size_t start = *pos;
  size_t i = start;
  while (i < s.size()) {
    char c = s[i];
    if (c == ',' || c == ']' || c == '}') break;
    if (stop_on_colon && c == ':' &&
        (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t'))
      break;
    ++i;
  }
  *pos = i;
  return rstrip(s.substr(start, i - start));
}

// Parse a single flow item starting at *pos. May recurse into nested
// flow. Returns the item, advances *pos past it.
an<ConfigItem> parse_flow_item(const string& s, size_t* pos, string* err) {
  skip_ws(s, pos);
  if (*pos >= s.size()) return an<ConfigItem>();
  char c = s[*pos];
  if (c == '[' || c == '{') {
    return parse_flow_value(s, pos, err);
  }
  if (c == '\'') {
    bool ok = false;
    string tok = scan_quoted(s, pos, &ok);
    if (!ok) { if (err) *err = "unterminated single quote"; return an<ConfigItem>(); }
    string val;
    if (!unquote_single(tok, &val)) { if (err) *err = "bad single-quoted string"; return an<ConfigItem>(); }
    return New<ConfigValue>(val);
  }
  if (c == '"') {
    bool ok = false;
    string tok = scan_quoted(s, pos, &ok);
    if (!ok) { if (err) *err = "unterminated double quote"; return an<ConfigItem>(); }
    string val;
    if (!unquote_double(tok, &val)) { if (err) *err = "bad double-quoted string"; return an<ConfigItem>(); }
    return New<ConfigValue>(val);
  }
  // Plain scalar inside flow: stop on , ] } and on a "map colon".
  string tok = scan_plain(s, pos, true);
  return scalar_to_value(tok);
}

// Parse a flow value (sequence or mapping) starting AT the '[' or '{'.
// Advances *pos past the matching ']' or '}'.
an<ConfigItem> parse_flow_value(const string& s, size_t* pos, string* err) {
  if (*pos >= s.size()) return an<ConfigItem>();
  char opener = s[*pos];
  char closer = (opener == '[') ? ']' : '}';
  bool is_map = (opener == '{');
  ++(*pos);  // skip opener

  if (is_map) {
    an<ConfigMap> mp = New<ConfigMap>();
    skip_ws(s, pos);
    if (*pos < s.size() && s[*pos] == closer) { ++(*pos); return mp; }
    while (*pos < s.size()) {
      skip_ws(s, pos);
      // Parse key: quoted or plain (no colon in plain key).
      string key;
      if (*pos < s.size() && (s[*pos] == '\'' || s[*pos] == '"')) {
        bool ok = false;
        string tok = scan_quoted(s, pos, &ok);
        if (!ok) { if (err) *err = "unterminated quote in flow key"; return an<ConfigItem>(); }
        key = unquote_key(tok);
      } else {
        key = strip(scan_plain(s, pos, true));
      }
      skip_ws(s, pos);
      if (*pos >= s.size() || s[*pos] != ':') {
        if (err) *err = "expected ':' in flow mapping";
        return an<ConfigItem>();
      }
      ++(*pos);  // skip ':'
      skip_ws(s, pos);
      an<ConfigItem> val = parse_flow_item(s, pos, err);
      if (err && !err->empty()) return an<ConfigItem>();
      mp->Set(key, val);
      skip_ws(s, pos);
      if (*pos < s.size() && s[*pos] == ',') { ++(*pos); continue; }
      if (*pos < s.size() && s[*pos] == closer) { ++(*pos); return mp; }
      if (err) *err = "expected ',' or '}' in flow mapping";
      return an<ConfigItem>();
    }
    if (err) *err = "unterminated flow mapping";
    return an<ConfigItem>();
  }

  an<ConfigList> ls = New<ConfigList>();
  skip_ws(s, pos);
  if (*pos < s.size() && s[*pos] == closer) { ++(*pos); return ls; }
  while (*pos < s.size()) {
    skip_ws(s, pos);
    an<ConfigItem> item = parse_flow_item(s, pos, err);
    if (err && !err->empty()) return an<ConfigItem>();
    ls->Append(item);
    skip_ws(s, pos);
    if (*pos < s.size() && s[*pos] == ',') { ++(*pos); continue; }
    if (*pos < s.size() && s[*pos] == closer) { ++(*pos); return ls; }
    if (err) *err = "expected ',' or ']' in flow sequence";
    return an<ConfigItem>();
  }
  if (err) *err = "unterminated flow sequence";
  return an<ConfigItem>();
}

// Parse a top-level inline value from a string (the part after "key:")
// or a sequence item ("- value"). Consumes the whole string; trailing
// garbage produces an error.
an<ConfigItem> parse_inline_value(const string& raw, string* err) {
  string s = strip(raw);
  if (s.empty()) return an<ConfigItem>();
  size_t pos = 0;
  string local_err;
  string* perr = err ? err : &local_err;
  an<ConfigItem> v;
  if (s[0] == '[' || s[0] == '{') {
    v = parse_flow_value(s, &pos, perr);
  } else if (s[0] == '\'') {
    bool ok = false;
    string tok = scan_quoted(s, &pos, &ok);
    if (!ok) { if (perr) *perr = "unterminated single quote"; return an<ConfigItem>(); }
    string val;
    if (!unquote_single(tok, &val)) { if (perr) *perr = "bad single-quoted string"; return an<ConfigItem>(); }
    v = New<ConfigValue>(val);
  } else if (s[0] == '"') {
    bool ok = false;
    string tok = scan_quoted(s, &pos, &ok);
    if (!ok) { if (perr) *perr = "unterminated double quote"; return an<ConfigItem>(); }
    string val;
    if (!unquote_double(tok, &val)) { if (perr) *perr = "bad double-quoted string"; return an<ConfigItem>(); }
    v = New<ConfigValue>(val);
  } else {
    // Plain scalar -- the WHOLE remainder is one token (no flow separators
    // at top level).
    v = scalar_to_value(s);
    pos = s.size();
  }
  if (perr && !perr->empty()) return an<ConfigItem>();
  // Allow trailing whitespace only.
  skip_ws(s, &pos);
  if (pos != s.size()) {
    if (err) *err = "trailing garbage after value";
    return an<ConfigItem>();
  }
  return v;
}

// ------------------------------------------------------------------
// Block parser. Holds a cursor into the PhysLine vector.
// ------------------------------------------------------------------
class BlockParser {
 public:
  BlockParser(const vector<PhysLine>* lines, string* err)
      : lines_(*lines), pos_(0), err_(err) {}

  // Skip past optional front-matter "---" .. "..." wrapper.
  void SkipFrontMatter() {
    SkipBlanks();
    if (pos_ < lines_.size() && lines_[pos_].is_doc_open) {
      ++pos_;
      SkipBlanks();
    }
  }

  // Parse the document body at indent > parent_indent. Returns null if
  // nothing to parse (empty doc) or on error.
  an<ConfigItem> ParseValue(int parent_indent) {
    SkipBlanks();
    if (AtEndOrTerminator()) return an<ConfigItem>();
    if (lines_[pos_].indent <= parent_indent) return an<ConfigItem>();
    int my_indent = lines_[pos_].indent;
    if (starts_with(lines_[pos_].content, "- ") ||
        lines_[pos_].content == "-") {
      return ParseSequence(my_indent);
    }
    return ParseMapping(my_indent);
  }

  bool finished() const {
    return pos_ >= lines_.size() ||
           (lines_[pos_].is_doc_close && lines_[pos_].indent == 0);
  }

 private:
  bool AtEndOrTerminator() {
    if (pos_ >= lines_.size()) return true;
    if (lines_[pos_].is_doc_close) return true;
    return false;
  }

  void SkipBlanks() {
    while (pos_ < lines_.size() && lines_[pos_].is_blank) ++pos_;
  }

  void Fail(size_t lineno, const char* what) {
    if (err_ && err_->empty()) {
      std::ostringstream os;
      os << "line " << lineno << ": " << what;
      *err_ = os.str();
    }
  }

  void Fail(size_t lineno, const string& what) {
    if (err_ && err_->empty()) {
      std::ostringstream os;
      os << "line " << lineno << ": " << what;
      *err_ = os.str();
    }
  }

  // Split a "key: rest" line. Returns (key, rest_or_empty). Caller
  // checks isBlank for rest to decide whether sub-block follows.
  bool SplitKeyAndRest(const string& s, string* key, string* rest) {
    size_t colon = find_map_colon(s);
    if (colon == string::npos) return false;
    *key = unquote_key(strip(s.substr(0, colon)));
    *rest = strip(s.substr(colon + 1));
    return true;
  }

  // Parse a value that follows a "key:" on the same logical line.
  // Special cases:
  //   * "|"  -> block literal scalar collected from subsequent lines
  //   * empty -> ParseValue(parent_indent)
  //   * else -> parse_inline_value
  an<ConfigItem> ParseValueAfterKey(const string& rest_in, int parent_indent,
                                    size_t lineno) {
    string rest = strip(rest_in);
    if (rest.empty()) {
      return ParseValue(parent_indent);
    }
    if (rest == "|" || rest == "|-" || rest == "|+") {
      return ParseBlockLiteral(parent_indent, rest);
    }
    string ierr;
    an<ConfigItem> v = parse_inline_value(rest, &ierr);
    if (!ierr.empty()) {
      Fail(lineno, ierr);
      return an<ConfigItem>();
    }
    return v;
  }

  // Collect a block literal scalar: all lines whose indent > parent_indent
  // (or blank). Strip the smallest common indent of the first non-blank
  // line; join with '\n'. Chomp mode: default "clip" (single trailing
  // newline), "-" strip all trailing newlines, "+" keep all.
  an<ConfigItem> ParseBlockLiteral(int parent_indent, const string& marker) {
    SkipBlanks();
    int base = -1;
    vector<string> buf;
    while (pos_ < lines_.size()) {
      const PhysLine& l = lines_[pos_];
      if (l.is_doc_close && l.indent == 0) break;
      if (l.is_blank) { buf.push_back(string()); ++pos_; continue; }
      if (l.indent <= parent_indent) break;
      if (base < 0) base = l.indent;
      int rel = l.indent - base;
      if (rel < 0) rel = 0;
      string line;
      line.assign(static_cast<size_t>(rel), ' ');
      line += l.content;
      buf.push_back(line);
      ++pos_;
    }
    // Trim leading blank lines.
    size_t front = 0;
    while (front < buf.size() && buf[front].empty()) ++front;
    // Build result.
    string result;
    for (size_t i = front; i < buf.size(); ++i) {
      if (i > front) result += '\n';
      result += buf[i];
    }
    // Chomp.
    if (marker == "|-") {
      while (!result.empty() && result[result.size() - 1] == '\n')
        result.resize(result.size() - 1);
    } else if (marker == "|+") {
      result += '\n';  // permissive: ensure at least one trailing newline
    } else {
      // "clip": exactly one trailing newline if there was any content.
      while (result.size() >= 2 &&
             result[result.size() - 1] == '\n' &&
             result[result.size() - 2] == '\n')
        result.resize(result.size() - 1);
      if (!result.empty() && result[result.size() - 1] != '\n')
        result += '\n';
    }
    return New<ConfigValue>(result);
  }

  an<ConfigItem> ParseMapping(int map_indent) {
    an<ConfigMap> mp = New<ConfigMap>();
    while (pos_ < lines_.size()) {
      SkipBlanks();
      if (AtEndOrTerminator()) break;
      const PhysLine& l = lines_[pos_];
      if (l.indent != map_indent) break;
      if (starts_with(l.content, "- ") || l.content == "-") break;
      string key, rest;
      if (!SplitKeyAndRest(l.content, &key, &rest)) {
        Fail(l.lineno, "expected mapping key:value");
        return an<ConfigItem>();
      }
      size_t kl = l.lineno;
      ++pos_;
      an<ConfigItem> v = ParseValueAfterKey(rest, map_indent, kl);
      if (err_ && !err_->empty()) return an<ConfigItem>();
      mp->Set(key, v);
    }
    return mp;
  }

  an<ConfigItem> ParseSequence(int seq_indent) {
    an<ConfigList> ls = New<ConfigList>();
    while (pos_ < lines_.size()) {
      SkipBlanks();
      if (AtEndOrTerminator()) break;
      const PhysLine& l = lines_[pos_];
      if (l.indent != seq_indent) break;
      bool dash_only = (l.content == "-");
      if (!dash_only && !starts_with(l.content, "- ")) break;
      string rest = dash_only ? string() : strip(l.content.substr(2));
      size_t ll = l.lineno;
      ++pos_;
      an<ConfigItem> item;
      if (rest.empty()) {
        item = ParseValue(seq_indent);  // block sub-value at indent > seq
      } else if (looks_like_map_start(rest) &&
                 rest[0] != '[' && rest[0] != '{' &&
                 rest[0] != '\'' && rest[0] != '"') {
        // Sequence-of-maps shorthand: "- key: val"
        an<ConfigMap> mp = New<ConfigMap>();
        string key, val_rest;
        if (!SplitKeyAndRest(rest, &key, &val_rest)) {
          Fail(ll, "bad map start after dash");
          return an<ConfigItem>();
        }
        // Sub-pairs of this item live at indent (seq_indent + 2).
        int item_indent = seq_indent + 2;
        an<ConfigItem> v = ParseValueAfterKey(val_rest, item_indent, ll);
        if (err_ && !err_->empty()) return an<ConfigItem>();
        mp->Set(key, v);
        // Continue parsing more key/value pairs at item_indent.
        while (pos_ < lines_.size()) {
          SkipBlanks();
          if (AtEndOrTerminator()) break;
          const PhysLine& nl = lines_[pos_];
          if (nl.indent != item_indent) break;
          if (starts_with(nl.content, "- ") || nl.content == "-") break;
          string k2, r2;
          if (!SplitKeyAndRest(nl.content, &k2, &r2)) {
            Fail(nl.lineno, "expected mapping key:value");
            return an<ConfigItem>();
          }
          size_t kl2 = nl.lineno;
          ++pos_;
          an<ConfigItem> v2 = ParseValueAfterKey(r2, item_indent, kl2);
          if (err_ && !err_->empty()) return an<ConfigItem>();
          mp->Set(k2, v2);
        }
        item = mp;
      } else {
        // Inline scalar / flow / quoted string as the item itself.
        string ierr;
        item = parse_inline_value(rest, &ierr);
        if (!ierr.empty()) { Fail(ll, ierr); return an<ConfigItem>(); }
      }
      ls->Append(item);
    }
    return ls;
  }

  const vector<PhysLine>& lines_;
  size_t pos_;
  string* err_;
};

}  // namespace

an<ConfigItem> Parse(const string& text, string* error) {
  if (error) error->clear();
  vector<PhysLine> lines;
  if (!preprocess(text, &lines, error)) return an<ConfigItem>();

  string local_err;
  string* perr = error ? error : &local_err;
  BlockParser p(&lines, perr);
  p.SkipFrontMatter();
  an<ConfigItem> root = p.ParseValue(-1);
  if (perr && !perr->empty()) return an<ConfigItem>();
  return root;
}

}  // namespace yaml
}  // namespace rime
