//
// rime/algo/syllabifier.cc -- WinCE-port mirror of upstream syllabifier.cc.
//
// Builds the SyllableGraph by repeatedly calling Prism::CommonPrefixSearch
// over input substrings. The MVP path has corrector_ == NULL; the
// correction branch is preserved verbatim (against the corrector stub)
// so the surface stays identical, but never executes.
//
// Changes vs. upstream:
//   * `using Vertex = pair<...>` -> typedef.
//   * `std::priority_queue<Vertex, vector<Vertex>, std::greater<Vertex>>`
//     -> `std::priority_queue<Vertex, vector<Vertex>, std::greater<Vertex> >`
//     (MSVC9 `> >` rule).
//   * `auto` everywhere -> explicit STL iterator types.
//   * Range-for `for (const auto& m : matches)` -> classic iterator loop.
//   * `queue.push(Vertex{0, kNormalSpelling})` brace-init ->
//     `Vertex tmp(0, kNormalSpelling); queue.push(tmp);` (Vertex == pair).
//   * `matches.push_back({m.first, m.second.length})` for Prism::Match
//     (POD struct from darts.h) -> temporary Match struct then push_back.
//   * `spellings.insert({syllable_id, props})` -> std::make_pair.
//   * `boost::adaptors::reverse(start.second)` -> manual reverse iterator
//     loop with map::reverse_iterator (the original code wants reverse
//     traversal of an EndVertexMap so that earlier end positions get
//     pushed AFTER later ones, preserving priority order in the index).
//
#include <algorithm>
#include <queue>
#include <rime/algo/syllabifier.h>
#include <rime/dict/corrector.h>
#include <rime/dict/prism.h>

namespace rime {

typedef pair<size_t, SpellingType> Vertex;
typedef std::priority_queue<Vertex, vector<Vertex>, std::greater<Vertex> >
    VertexQueue;

// Weight ladder:
// 1. Full pinyin: user typed every code. Penalty = 0
// 2. Abbreviation: user typed shorthand. Penalty ~= -2.3
// 3. Completion: user hasn't finished. Penalty ~= -3.0
const double kCompletionPenalty = -2.995732273553991;      // log(0.05)
const double kCorrectionCredibility = -4.605170185988091;  // log(0.01)

int Syllabifier::BuildSyllableGraph(const string& input,
                                    Prism& prism,
                                    SyllableGraph* graph) {
  if (input.empty())
    return 0;

  size_t farthest = 0;
  VertexQueue queue;
  {
    Vertex start_vertex(0, kNormalSpelling);
    queue.push(start_vertex);
  }

  while (!queue.empty()) {
    Vertex vertex(queue.top());
    queue.pop();
    size_t current_pos = vertex.first;

    // record a visit to the vertex
    if (graph->vertices.find(current_pos) == graph->vertices.end()) {
      graph->vertices.insert(vertex);  // preferred spelling type comes first
    } else {
      continue;  // discard worse spelling types
    }

    if (current_pos > farthest)
      farthest = current_pos;

    // consume leading delimiters
    size_t begin_pos = current_pos;
    while (begin_pos < input.length() &&
           delimiters_.find(input[begin_pos]) != string::npos)
      ++begin_pos;
    DLOG(INFO) << "current_pos: " << current_pos
               << ", begin_pos: " << begin_pos;

    // see where we can go by advancing a syllable
    vector<Prism::Match> matches;
    set<SyllableId> exact_match_syllables;
    string current_input = input.substr(begin_pos);
    prism.CommonPrefixSearch(current_input, &matches);
    if (corrector_) {
      for (vector<Prism::Match>::iterator mit = matches.begin();
           mit != matches.end(); ++mit) {
        exact_match_syllables.insert(mit->value);
      }
      corrector::Corrections corrections;
      corrector_->ToleranceSearch(prism, current_input, &corrections, 5);
      for (corrector::Corrections::const_iterator cit = corrections.begin();
           cit != corrections.end(); ++cit) {
        for (SpellingAccessor accessor = prism.QuerySpelling(cit->first);
             !accessor.exhausted(); accessor.Next()) {
          SpellingProperties props = accessor.properties();
          if (props.type == kNormalSpelling && !props.is_correction) {
            Prism::Match m;
            m.value = cit->first;
            m.length = cit->second.length;
            matches.push_back(m);
            break;
          }
        }
      }
    }

    size_t leading_gap = begin_pos - current_pos;
    if (!matches.empty()) {
      EndVertexMap& end_vertices = graph->edges[current_pos];
      for (vector<Prism::Match>::const_iterator mit = matches.begin();
           mit != matches.end(); ++mit) {
        const Prism::Match& m = *mit;
        if (m.length == 0)
          continue;
        size_t end_pos = current_pos + leading_gap + m.length;
        // consume trailing delimiters
        while (end_pos < input.length() &&
               delimiters_.find(input[end_pos]) != string::npos)
          ++end_pos;
        DLOG(INFO) << "end_pos: " << end_pos;
        bool matches_input = (current_pos == 0 && end_pos == input.length());
        SpellingMap& spellings = end_vertices[end_pos];
        SpellingType end_vertex_type = kInvalidSpelling;
        SpellingAccessor accessor(prism.QuerySpelling(m.value));
        while (!accessor.exhausted()) {
          SyllableId syllable_id = accessor.syllable_id();
          EdgeProperties props(accessor.properties());
          if (strict_spelling_ && matches_input &&
              props.type != kNormalSpelling) {
            // disqualify fuzzy spelling or abbreviation as single word
          } else {
            props.end_pos = end_pos;
            if (corrector_ && exact_match_syllables.find(m.value) ==
                                  exact_match_syllables.end()) {
              props.is_correction = true;
              props.credibility = kCorrectionCredibility;
            }
            SpellingMap::iterator it = spellings.find(syllable_id);
            if (it == spellings.end()) {
              spellings.insert(std::make_pair(syllable_id, props));
            } else {
              it->second.type = (std::min)(it->second.type, props.type);
            }
            if (end_vertex_type > props.type && !props.is_correction) {
              end_vertex_type = props.type;
            }
          }
          accessor.Next();
        }
        if (spellings.empty()) {
          DLOG(INFO) << "not spelled.";
          end_vertices.erase(end_pos);
          continue;
        }
        // find the best common type in a path up to the end vertex
        if (end_vertex_type < vertex.second) {
          end_vertex_type = vertex.second;
        }
        Vertex next_vertex(end_pos, end_vertex_type);
        queue.push(next_vertex);
        DLOG(INFO) << "added to syllable graph, edge: [" << current_pos << ", "
                   << end_pos << ")";
      }
    }
  }

  DLOG(INFO) << "remove stale vertices and edges";
  set<int> good;
  good.insert((int)farthest);
  // fuzzy spellings are immune to invalidation by normal spellings
  SpellingType last_type =
      (std::max)(graph->vertices[farthest], kFuzzySpelling);
  for (int i = (int)farthest - 1; i >= 0; --i) {
    if (graph->vertices.find(i) == graph->vertices.end())
      continue;
    // remove stale edges
    for (EndVertexMap::iterator j = graph->edges[i].begin();
         j != graph->edges[i].end();) {
      if (good.find((int)j->first) == good.end()) {
        // not connected
        graph->edges[i].erase(j++);
        continue;
      }
      SpellingType edge_type = kInvalidSpelling;
      for (SpellingMap::iterator k = j->second.begin();
           k != j->second.end();) {
        if (k->second.is_correction) {
          ++k;
          continue;
        }
        if (k->second.type > last_type) {
          j->second.erase(k++);
        } else {
          if (k->second.type < edge_type)
            edge_type = k->second.type;
          ++k;
        }
      }
      if (j->second.empty()) {
        graph->edges[i].erase(j++);
      } else {
        if (edge_type < kAbbreviation)
          CheckOverlappedSpellings(graph, i, j->first);
        ++j;
      }
    }
    if (graph->vertices[i] > last_type || graph->edges[i].empty()) {
      DLOG(INFO) << "remove stale vertex at " << i;
      graph->vertices.erase(i);
      graph->edges.erase(i);
      continue;
    }
    good.insert(i);
  }

  if (enable_completion_ && farthest < input.length()) {
    DLOG(INFO) << "completion enabled";
    const size_t kExpandSearchLimit = 512;
    vector<Prism::Match> keys;
    prism.ExpandSearch(input.substr(farthest), &keys, kExpandSearchLimit);
    if (!keys.empty()) {
      size_t current_pos = farthest;
      size_t end_pos = input.length();
      size_t code_length = end_pos - current_pos;
      EndVertexMap& end_vertices = graph->edges[current_pos];
      SpellingMap& spellings = end_vertices[end_pos];
      for (vector<Prism::Match>::const_iterator kit = keys.begin();
           kit != keys.end(); ++kit) {
        const Prism::Match& m = *kit;
        if (m.length < code_length)
          continue;
        SpellingAccessor accessor(prism.QuerySpelling(m.value));
        while (!accessor.exhausted()) {
          SyllableId syllable_id = accessor.syllable_id();
          SpellingProperties props = accessor.properties();
          if (props.type < kAbbreviation) {
            props.type = kCompletion;
            props.credibility += kCompletionPenalty;
            props.end_pos = end_pos;
            // Upstream brace-init here; EdgeProperties is constructible
            // from SpellingProperties via the converting ctor.
            EdgeProperties ep(props);
            spellings.insert(std::make_pair(syllable_id, ep));
          }
          accessor.Next();
        }
      }
      if (spellings.empty()) {
        DLOG(INFO) << "no completion could be made.";
        end_vertices.erase(end_pos);
      } else {
        DLOG(INFO) << "added to syllable graph, completion: [" << current_pos
                   << ", " << end_pos << ")";
        farthest = end_pos;
      }
    }
  }

  graph->input_length = input.length();
  graph->interpreted_length = farthest;
  DLOG(INFO) << "input length: " << graph->input_length;
  DLOG(INFO) << "syllabified length: " << graph->interpreted_length;

  Transpose(graph);

  return (int)farthest;
}

void Syllabifier::CheckOverlappedSpellings(SyllableGraph* graph,
                                           size_t start,
                                           size_t end) {
  if (!graph || graph->edges.find(start) == graph->edges.end())
    return;
  // if "Z" = "YX", mark the vertex between Y and X an ambiguous syllable joint
  EndVertexMap& y_end_vertices = graph->edges[start];
  // enumerate Ys
  for (EndVertexMap::const_iterator yit = y_end_vertices.begin();
       yit != y_end_vertices.end(); ++yit) {
    size_t joint = yit->first;
    if (joint >= end)
      break;
    // test X
    if (graph->edges.find(joint) == graph->edges.end())
      continue;
    EndVertexMap& x_end_vertices = graph->edges[joint];
    for (EndVertexMap::iterator xit = x_end_vertices.begin();
         xit != x_end_vertices.end(); ++xit) {
      if (xit->first < end)
        continue;
      if (xit->first == end) {
        // discourage syllables at an ambiguous joint
        for (SpellingMap::iterator sit = xit->second.begin();
             sit != xit->second.end(); ++sit) {
          // This edge (X) is ambiguous relative to start.
          sit->second.ambiguous_source_positions.insert(start);
        }
        graph->vertices[joint] = kAmbiguousSpelling;
        DLOG(INFO) << "ambiguous syllable joint at position " << joint << ".";
      }
      break;
    }
  }
}

void Syllabifier::Transpose(SyllableGraph* graph) {
  for (EdgeMap::const_iterator sit = graph->edges.begin();
       sit != graph->edges.end(); ++sit) {
    SpellingIndex& index = graph->indices[sit->first];
    // Upstream uses boost::adaptors::reverse(sit->second) to walk the
    // end-vertices in reverse; map.reverse_iterator gives the same order.
    const EndVertexMap& end_map = sit->second;
    for (EndVertexMap::const_reverse_iterator eit = end_map.rbegin();
         eit != end_map.rend(); ++eit) {
      for (SpellingMap::const_iterator spit = eit->second.begin();
           spit != eit->second.end(); ++spit) {
        SyllableId syll_id = spit->first;
        index[syll_id].push_back(&spit->second);
      }
    }
  }
}

void Syllabifier::EnableCorrection(Corrector* corrector) {
  corrector_ = corrector;
}

}  // namespace rime
