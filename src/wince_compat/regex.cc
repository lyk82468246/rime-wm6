//
// wince_compat/regex.cc -- Thompson-NFA / Pike-VM regex engine.
//
// Inspired by Russ Cox's "Regular Expression Matching: The Virtual Machine
// Approach". O(n * m) worst case, no catastrophic backtracking.
//
// Architecture (top to bottom in this file):
//   1. Data types: char_class / Inst / prog (the compiled program).
//   2. AST nodes: produced by the parser, consumed by the compiler.
//   3. Parser: recursive descent over the pattern string.
//   4. Compiler: walks the AST, emits bytecode into prog.code.
//   5. Engine: Pike VM. Runs the bytecode on a subject string.
//   6. Public API: regex / smatch / regex_match / search / replace.
//
#include "regex.h"

#include <cstring>

namespace wince {

namespace re_detail {

// ===========================================================================
// 1. Data types
// ===========================================================================

struct char_class {
  bool match[256];
  char_class() {
    for (int i = 0; i < 256; ++i) match[i] = false;
  }
};

enum Op {
  OP_CHAR = 1,
  OP_DOT,
  OP_CLASS,
  OP_BOL,
  OP_EOL,
  OP_JMP,
  OP_SPLIT,
  OP_SAVE,
  OP_MATCH
};

struct Inst {
  int op;
  int a;
  int b;
  Inst() : op(0), a(0), b(0) {}
};

struct prog {
  std::vector<Inst> code;
  std::vector<char_class> classes;
  // Number of capture groups, INCLUDING the implicit group 0 (whole match).
  // Saves array size is 2 * num_groups.
  int num_groups;
  prog() : num_groups(1) {}
};

// ===========================================================================
// 2. AST
// ===========================================================================

enum NodeType {
  NT_EMPTY,
  NT_LIT,
  NT_DOT,
  NT_BOL,
  NT_EOL,
  NT_CLASS,
  NT_STAR,
  NT_PLUS,
  NT_QUEST,
  NT_CAT,
  NT_ALT,
  NT_GROUP
};

struct Node {
  int type;
  int ch;          // for NT_LIT
  int cls_idx;     // for NT_CLASS (index into prog.classes)
  int group_idx;   // for NT_GROUP
  shared_ptr<Node> left;
  shared_ptr<Node> right;
  Node() : type(NT_EMPTY), ch(0), cls_idx(-1), group_idx(0) {}
};

// ===========================================================================
// 3. Parser (recursive descent)
//
// Grammar:
//   alt     = cat ('|' cat)*
//   cat     = quant*                       -- empty cat allowed (matches "")
//   quant   = atom ('*' | '+' | '?')?
//   atom    = '.' | '^' | '$' | '(' alt ')' | '[' class ']' | escape | literal
// ===========================================================================

class Parser {
 public:
  Parser(const char* p, size_t n, prog& output)
      : p_(p), end_(p + n), prog_(output), next_group_(1) {}

  shared_ptr<Node> parse_all() {
    shared_ptr<Node> root = parse_alt();
    if (p_ != end_) {
      throw regex_error("regex: unexpected character at end of pattern");
    }
    prog_.num_groups = next_group_;  // includes group 0
    return root;
  }

 private:
  shared_ptr<Node> parse_alt();
  shared_ptr<Node> parse_cat();
  shared_ptr<Node> parse_quant();
  shared_ptr<Node> parse_atom();
  shared_ptr<Node> make_class_from_escape(char esc);
  int parse_class_char();

  bool peek(char c) const { return p_ < end_ && *p_ == c; }
  bool at_end() const { return p_ >= end_; }

  const char* p_;
  const char* end_;
  prog& prog_;
  int next_group_;
};

shared_ptr<Node> Parser::parse_alt() {
  shared_ptr<Node> left = parse_cat();
  while (peek('|')) {
    ++p_;
    shared_ptr<Node> right = parse_cat();
    shared_ptr<Node> n(new Node);
    n->type = NT_ALT;
    n->left = left;
    n->right = right;
    left = n;
  }
  return left;
}

shared_ptr<Node> Parser::parse_cat() {
  shared_ptr<Node> root;
  while (!at_end() && *p_ != '|' && *p_ != ')') {
    shared_ptr<Node> q = parse_quant();
    if (!root) {
      root = q;
    } else {
      shared_ptr<Node> n(new Node);
      n->type = NT_CAT;
      n->left = root;
      n->right = q;
      root = n;
    }
  }
  if (!root) {
    root.reset(new Node);
    root->type = NT_EMPTY;
  }
  return root;
}

shared_ptr<Node> Parser::parse_quant() {
  shared_ptr<Node> atom = parse_atom();
  if (peek('*') || peek('+') || peek('?')) {
    shared_ptr<Node> q(new Node);
    if (*p_ == '*') q->type = NT_STAR;
    else if (*p_ == '+') q->type = NT_PLUS;
    else                  q->type = NT_QUEST;
    q->left = atom;
    ++p_;
    return q;
  }
  return atom;
}

shared_ptr<Node> Parser::parse_atom() {
  if (at_end()) throw regex_error("regex: unexpected end of pattern");
  char c = *p_;

  if (c == '.') {
    ++p_;
    shared_ptr<Node> n(new Node);
    n->type = NT_DOT;
    return n;
  }
  if (c == '^') {
    ++p_;
    shared_ptr<Node> n(new Node);
    n->type = NT_BOL;
    return n;
  }
  if (c == '$') {
    ++p_;
    shared_ptr<Node> n(new Node);
    n->type = NT_EOL;
    return n;
  }
  if (c == '(') {
    ++p_;
    int g = next_group_++;
    shared_ptr<Node> inner = parse_alt();
    if (at_end() || *p_ != ')') {
      throw regex_error("regex: unmatched '('");
    }
    ++p_;  // consume ')'
    shared_ptr<Node> n(new Node);
    n->type = NT_GROUP;
    n->group_idx = g;
    n->left = inner;
    return n;
  }
  if (c == '[') {
    ++p_;  // consume '['
    char_class cls;
    bool negate = false;
    if (peek('^')) { negate = true; ++p_; }
    bool first = true;
    while (!at_end() && (*p_ != ']' || first)) {
      first = false;
      int ch1 = parse_class_char();
      if (peek('-') && (p_ + 1) < end_ && *(p_ + 1) != ']') {
        ++p_;  // consume '-'
        int ch2 = parse_class_char();
        if (ch1 > ch2) {
          throw regex_error("regex: invalid range in '[...]'");
        }
        for (int i = ch1; i <= ch2; ++i) cls.match[i] = true;
      } else {
        cls.match[ch1] = true;
      }
    }
    if (at_end()) throw regex_error("regex: unterminated '['");
    ++p_;  // consume ']'
    if (negate) {
      for (int i = 0; i < 256; ++i) cls.match[i] = !cls.match[i];
    }
    int idx = static_cast<int>(prog_.classes.size());
    prog_.classes.push_back(cls);
    shared_ptr<Node> n(new Node);
    n->type = NT_CLASS;
    n->cls_idx = idx;
    return n;
  }
  if (c == '\\') {
    if (p_ + 1 >= end_) throw regex_error("regex: trailing '\\'");
    char esc = *(p_ + 1);
    // Class-shorthand escapes outside of '[...]'.
    if (esc == 'd' || esc == 'D' ||
        esc == 'w' || esc == 'W' ||
        esc == 's' || esc == 'S') {
      shared_ptr<Node> n = make_class_from_escape(esc);
      p_ += 2;
      return n;
    }
    // Literal escape: translate common escape codes; otherwise the next byte
    // is taken literally (covers \. \\ \( \) \[ \] \| \^ \$ \* \+ \? \/ etc.).
    int ch;
    if      (esc == 'n') ch = '\n';
    else if (esc == 't') ch = '\t';
    else if (esc == 'r') ch = '\r';
    else if (esc == 'f') ch = '\f';
    else if (esc == '0') ch = '\0';
    else                 ch = (unsigned char)esc;
    p_ += 2;
    shared_ptr<Node> n(new Node);
    n->type = NT_LIT;
    n->ch = ch;
    return n;
  }
  if (c == '*' || c == '+' || c == '?' || c == ')' || c == '|') {
    throw regex_error("regex: unexpected metacharacter");
  }
  // Plain literal byte.
  ++p_;
  shared_ptr<Node> n(new Node);
  n->type = NT_LIT;
  n->ch = (unsigned char)c;
  return n;
}

shared_ptr<Node> Parser::make_class_from_escape(char esc) {
  char_class cls;
  bool negate = (esc == 'D' || esc == 'W' || esc == 'S');
  if (esc == 'd' || esc == 'D') {
    for (int i = '0'; i <= '9'; ++i) cls.match[i] = true;
  } else if (esc == 'w' || esc == 'W') {
    for (int i = '0'; i <= '9'; ++i) cls.match[i] = true;
    for (int i = 'a'; i <= 'z'; ++i) cls.match[i] = true;
    for (int i = 'A'; i <= 'Z'; ++i) cls.match[i] = true;
    cls.match[(int)'_'] = true;
  } else {  // s / S
    cls.match[(int)' ']  = true;
    cls.match[(int)'\t'] = true;
    cls.match[(int)'\n'] = true;
    cls.match[(int)'\r'] = true;
    cls.match[(int)'\f'] = true;
  }
  if (negate) {
    for (int i = 0; i < 256; ++i) cls.match[i] = !cls.match[i];
  }
  int idx = static_cast<int>(prog_.classes.size());
  prog_.classes.push_back(cls);
  shared_ptr<Node> n(new Node);
  n->type = NT_CLASS;
  n->cls_idx = idx;
  return n;
}

int Parser::parse_class_char() {
  if (at_end()) throw regex_error("regex: unterminated '['");
  char c = *p_;
  if (c == '\\') {
    if (p_ + 1 >= end_) throw regex_error("regex: trailing '\\' in '[...]'");
    char esc = *(p_ + 1);
    int ch;
    if      (esc == 'n') ch = '\n';
    else if (esc == 't') ch = '\t';
    else if (esc == 'r') ch = '\r';
    else if (esc == 'f') ch = '\f';
    else if (esc == '0') ch = '\0';
    else                 ch = (unsigned char)esc;
    p_ += 2;
    return ch;
  }
  ++p_;
  return (unsigned char)c;
}

// ===========================================================================
// 4. Compiler (AST -> bytecode)
//
// Each node emits a sequence of instructions into prog.code. Quantifiers and
// alternation use SPLIT + JMP patches that are filled in once we know the
// downstream PC. PCs are absolute indices into prog.code.
// ===========================================================================

static void compile_node(prog& p, const Node& n) {
  switch (n.type) {
    case NT_EMPTY:
      // matches zero characters: emit nothing
      break;
    case NT_LIT: {
      Inst i; i.op = OP_CHAR; i.a = n.ch;
      p.code.push_back(i);
      break;
    }
    case NT_DOT: {
      Inst i; i.op = OP_DOT;
      p.code.push_back(i);
      break;
    }
    case NT_BOL: {
      Inst i; i.op = OP_BOL;
      p.code.push_back(i);
      break;
    }
    case NT_EOL: {
      Inst i; i.op = OP_EOL;
      p.code.push_back(i);
      break;
    }
    case NT_CLASS: {
      Inst i; i.op = OP_CLASS; i.a = n.cls_idx;
      p.code.push_back(i);
      break;
    }
    case NT_CAT: {
      compile_node(p, *n.left);
      compile_node(p, *n.right);
      break;
    }
    case NT_STAR: {
      // L1:  split L2, L3       (consume, then skip)
      // L2:  <body>
      //      jmp L1
      // L3:  ...
      int L1 = static_cast<int>(p.code.size());
      Inst split; split.op = OP_SPLIT;
      p.code.push_back(split);
      int L2 = static_cast<int>(p.code.size());
      compile_node(p, *n.left);
      Inst jmp; jmp.op = OP_JMP; jmp.a = L1;
      p.code.push_back(jmp);
      int L3 = static_cast<int>(p.code.size());
      p.code[L1].a = L2;
      p.code[L1].b = L3;
      break;
    }
    case NT_PLUS: {
      // L1:  <body>
      //      split L1, L2       (loop preferred)
      // L2:  ...
      int L1 = static_cast<int>(p.code.size());
      compile_node(p, *n.left);
      int LS = static_cast<int>(p.code.size());
      Inst split; split.op = OP_SPLIT; split.a = L1;
      p.code.push_back(split);
      int L2 = static_cast<int>(p.code.size());
      p.code[LS].b = L2;
      break;
    }
    case NT_QUEST: {
      //      split L1, L2       (take, then skip)
      // L1:  <body>
      // L2:  ...
      int LS = static_cast<int>(p.code.size());
      Inst split; split.op = OP_SPLIT;
      p.code.push_back(split);
      int L1 = static_cast<int>(p.code.size());
      compile_node(p, *n.left);
      int L2 = static_cast<int>(p.code.size());
      p.code[LS].a = L1;
      p.code[LS].b = L2;
      break;
    }
    case NT_ALT: {
      //      split L1, L2
      // L1:  <left>
      //      jmp L3
      // L2:  <right>
      // L3:  ...
      int LS = static_cast<int>(p.code.size());
      Inst split; split.op = OP_SPLIT;
      p.code.push_back(split);
      int L1 = static_cast<int>(p.code.size());
      compile_node(p, *n.left);
      int LJ = static_cast<int>(p.code.size());
      Inst jmp; jmp.op = OP_JMP;
      p.code.push_back(jmp);
      int L2 = static_cast<int>(p.code.size());
      compile_node(p, *n.right);
      int L3 = static_cast<int>(p.code.size());
      p.code[LS].a = L1;
      p.code[LS].b = L2;
      p.code[LJ].a = L3;
      break;
    }
    case NT_GROUP: {
      Inst sav_start; sav_start.op = OP_SAVE; sav_start.a = 2 * n.group_idx;
      p.code.push_back(sav_start);
      compile_node(p, *n.left);
      Inst sav_end; sav_end.op = OP_SAVE; sav_end.a = 2 * n.group_idx + 1;
      p.code.push_back(sav_end);
      break;
    }
  }
}

static void compile_program(prog& p, const Node& root) {
  // Implicit group 0 wraps the whole pattern.
  Inst sav0; sav0.op = OP_SAVE; sav0.a = 0;
  p.code.push_back(sav0);
  compile_node(p, root);
  Inst sav1; sav1.op = OP_SAVE; sav1.a = 1;
  p.code.push_back(sav1);
  Inst m; m.op = OP_MATCH;
  p.code.push_back(m);
}

// ===========================================================================
// 5. Engine (Pike VM)
//
// At each input position we maintain a list of currently-active threads, in
// priority order. A thread is (pc, saves). Epsilon transitions (JMP / SPLIT /
// SAVE / BOL / EOL) are walked eagerly in add_thread() until the thread sits
// on a consuming op (CHAR / DOT / CLASS) or on MATCH.
//
// Per-instruction "visited" generation counter de-duplicates threads at the
// same pc within one step, preserving the higher-priority one's captures.
//
// Leftmost-first semantics: when a thread reaches MATCH, lower-priority
// threads (later in clist) are killed. Higher-priority threads already
// processed and now in nlist may still extend the match.
// ===========================================================================

class Engine {
 public:
  const prog* p;
  const std::string* subject;

  bool matched;
  int match_start;
  int match_end;
  std::vector<int> match_saves;

  // Find a match starting at exactly `start_pos`. Returns true if any match
  // exists; on success sets match_start/match_end/match_saves.
  bool run(size_t start_pos);

 private:
  struct Thread {
    int pc;
    std::vector<int> saves;
  };
  std::vector<Thread> clist_;
  std::vector<Thread> nlist_;
  std::vector<int> visited_;
  int gen_;

  void add_thread(std::vector<Thread>& list,
                  int pc,
                  std::vector<int> saves,
                  size_t pos);
};

void Engine::add_thread(std::vector<Thread>& list,
                        int pc,
                        std::vector<int> saves,
                        size_t pos) {
  while (true) {
    if (pc < 0 || (size_t)pc >= visited_.size()) return;
    if (visited_[pc] == gen_) return;
    visited_[pc] = gen_;
    const Inst& I = p->code[pc];
    if (I.op == OP_JMP) {
      pc = I.a;
      continue;
    }
    if (I.op == OP_SPLIT) {
      // Higher-priority branch (a) first, so its descendants end up earlier
      // in the priority-ordered list.
      add_thread(list, I.a, saves, pos);
      pc = I.b;
      continue;
    }
    if (I.op == OP_SAVE) {
      if (I.a >= 0 && (size_t)I.a < saves.size()) {
        saves[I.a] = static_cast<int>(pos);
      }
      ++pc;
      continue;
    }
    if (I.op == OP_BOL) {
      if (pos == 0) { ++pc; continue; }
      return;
    }
    if (I.op == OP_EOL) {
      if (pos == subject->size()) { ++pc; continue; }
      return;
    }
    // OP_CHAR / OP_DOT / OP_CLASS / OP_MATCH: leaf for this step.
    Thread t;
    t.pc = pc;
    t.saves.swap(saves);
    list.push_back(t);
    return;
  }
}

bool Engine::run(size_t start_pos) {
  matched = false;
  match_start = static_cast<int>(start_pos);
  match_end = static_cast<int>(start_pos);

  visited_.assign(p->code.size(), -1);
  gen_ = 0;
  clist_.clear();
  nlist_.clear();

  std::vector<int> init_saves(2 * static_cast<size_t>(p->num_groups), -1);
  add_thread(clist_, 0, init_saves, start_pos);

  for (size_t pos = start_pos; ; ++pos) {
    if (clist_.empty()) break;
    ++gen_;
    nlist_.clear();

    for (size_t i = 0; i < clist_.size(); ++i) {
      Thread& t = clist_[i];
      const Inst& I = p->code[t.pc];
      if (I.op == OP_MATCH) {
        // Higher-priority match completed. Record and kill lower threads
        // (those still after i in clist_). Higher-priority threads earlier
        // in clist_ have already been processed -- if any of them extend
        // the match later they will overwrite match_end below.
        matched = true;
        match_end = static_cast<int>(pos);
        match_saves.swap(t.saves);
        break;
      }
      bool consumed = false;
      if (I.op == OP_CHAR) {
        if (pos < subject->size() &&
            (unsigned char)(*subject)[pos] == (unsigned char)I.a) {
          consumed = true;
        }
      } else if (I.op == OP_DOT) {
        if (pos < subject->size()) consumed = true;
      } else if (I.op == OP_CLASS) {
        if (pos < subject->size() &&
            p->classes[I.a].match[(unsigned char)(*subject)[pos]]) {
          consumed = true;
        }
      }
      if (consumed) {
        add_thread(nlist_, t.pc + 1, t.saves, pos + 1);
      }
    }

    if (pos >= subject->size()) break;
    clist_.swap(nlist_);
  }

  if (matched) {
    match_start = (match_saves.size() >= 2 && match_saves[0] >= 0)
                      ? match_saves[0]
                      : static_cast<int>(start_pos);
    // match_end may have been updated by a later iteration above.
    return true;
  }
  return false;
}

}  // namespace re_detail

// ===========================================================================
// 6. Public API
// ===========================================================================

regex::regex() {}

regex::regex(const char* pattern) { assign(pattern); }
regex::regex(const std::string& pattern) { assign(pattern); }

void regex::assign(const char* pattern) {
  size_t n = pattern ? std::strlen(pattern) : 0;
  shared_ptr<re_detail::prog> p(new re_detail::prog);
  re_detail::Parser parser(pattern, n, *p);
  shared_ptr<re_detail::Node> root = parser.parse_all();
  re_detail::compile_program(*p, *root);
  prog_ = p;
}

void regex::assign(const std::string& pattern) {
  shared_ptr<re_detail::prog> p(new re_detail::prog);
  re_detail::Parser parser(pattern.data(), pattern.size(), *p);
  shared_ptr<re_detail::Node> root = parser.parse_all();
  re_detail::compile_program(*p, *root);
  prog_ = p;
}

// ---------------------------------------------------------------------------
// smatch
// ---------------------------------------------------------------------------

size_t smatch::size() const {
  if (!matched_) return 0;
  return captures_.size() / 2;
}

std::string smatch::str(size_t n) const {
  if (!matched_) return std::string();
  size_t idx = 2 * n;
  if (idx + 1 >= captures_.size()) return std::string();
  int start = captures_[idx];
  int end = captures_[idx + 1];
  if (start < 0 || end < 0 || end < start) return std::string();
  return subject_.substr(static_cast<size_t>(start),
                         static_cast<size_t>(end - start));
}

std::string smatch::prefix() const {
  if (!matched_ || captures_.size() < 2 || captures_[0] < 0) return std::string();
  return subject_.substr(0, static_cast<size_t>(captures_[0]));
}

std::string smatch::suffix() const {
  if (!matched_ || captures_.size() < 2 || captures_[1] < 0) return std::string();
  return subject_.substr(static_cast<size_t>(captures_[1]));
}

int smatch::position(size_t n) const {
  size_t idx = 2 * n;
  if (!matched_ || idx >= captures_.size()) return -1;
  return captures_[idx];
}

int smatch::length(size_t n) const {
  size_t idx = 2 * n;
  if (!matched_ || idx + 1 >= captures_.size()) return 0;
  int s = captures_[idx];
  int e = captures_[idx + 1];
  if (s < 0 || e < 0) return 0;
  return e - s;
}

void smatch::assign(const std::string& subject,
                    size_t whole_start,
                    size_t whole_end,
                    const std::vector<int>& captures) {
  subject_ = subject;
  captures_ = captures;
  if (captures_.size() < 2) captures_.resize(2, -1);
  // Force group-0 entries to the canonical match span. The engine's SAVE 0/1
  // pair normally fills these in, but we overwrite to be defensive against
  // patterns where SAVE 0/1 might not execute (impossible in our compiler
  // today, but the cost is one assignment).
  captures_[0] = static_cast<int>(whole_start);
  captures_[1] = static_cast<int>(whole_end);
  matched_ = true;
}

void smatch::clear() {
  subject_.clear();
  captures_.clear();
  matched_ = false;
}

// ---------------------------------------------------------------------------
// regex_match / regex_search / regex_replace
// ---------------------------------------------------------------------------

bool regex_match(const std::string& s, const regex& re) {
  smatch m;
  return regex_match(s, m, re);
}

bool regex_match(const std::string& s, smatch& m, const regex& re) {
  m.clear();
  if (!re.program()) return false;
  re_detail::Engine e;
  e.p = re.program();
  e.subject = &s;
  if (!e.run(0)) return false;
  if ((size_t)e.match_end != s.size()) return false;
  m.assign(s, 0, static_cast<size_t>(e.match_end), e.match_saves);
  return true;
}

bool regex_search(const std::string& s, const regex& re) {
  smatch m;
  return regex_search(s, m, re);
}

bool regex_search(const std::string& s, smatch& m, const regex& re) {
  m.clear();
  if (!re.program()) return false;
  re_detail::Engine e;
  e.p = re.program();
  e.subject = &s;
  // Try every start position; return the leftmost match.
  for (size_t start = 0; start <= s.size(); ++start) {
    if (e.run(start)) {
      m.assign(s, start, static_cast<size_t>(e.match_end), e.match_saves);
      return true;
    }
  }
  return false;
}

// Expand $0..$9 and $$ in `fmt`, writing into `out`.
static void expand_replacement(std::string& out,
                               const std::string& fmt,
                               const std::string& subject,
                               const std::vector<int>& saves) {
  for (size_t i = 0; i < fmt.size(); ++i) {
    char c = fmt[i];
    if (c == '$' && i + 1 < fmt.size()) {
      char next = fmt[i + 1];
      if (next == '$') {
        out.push_back('$');
        ++i;
        continue;
      }
      if (next >= '0' && next <= '9') {
        size_t g = static_cast<size_t>(next - '0');
        size_t idx = 2 * g;
        if (idx + 1 < saves.size()) {
          int s = saves[idx];
          int e = saves[idx + 1];
          if (s >= 0 && e >= s) {
            out.append(subject, static_cast<size_t>(s),
                       static_cast<size_t>(e - s));
          }
        }
        ++i;
        continue;
      }
    }
    out.push_back(c);
  }
}

std::string regex_replace(const std::string& s,
                          const regex& re,
                          const std::string& fmt) {
  if (!re.program()) return s;
  std::string out;
  out.reserve(s.size());
  re_detail::Engine e;
  e.p = re.program();
  e.subject = &s;
  size_t pos = 0;
  while (pos <= s.size()) {
    // Find leftmost match starting at >= pos.
    size_t match_start = 0;
    bool found = false;
    for (size_t start = pos; start <= s.size(); ++start) {
      if (e.run(start)) {
        match_start = start;
        found = true;
        break;
      }
    }
    if (!found) {
      out.append(s, pos, s.size() - pos);
      break;
    }
    size_t match_end = static_cast<size_t>(e.match_end);
    // Copy the unmatched prefix.
    out.append(s, pos, match_start - pos);
    // Append the expanded replacement.
    expand_replacement(out, fmt, s, e.match_saves);
    // Force forward progress on an empty match by copying one byte through.
    if (match_end == match_start) {
      if (match_start < s.size()) out.push_back(s[match_start]);
      pos = match_start + 1;
    } else {
      pos = match_end;
    }
  }
  return out;
}

}  // namespace wince
