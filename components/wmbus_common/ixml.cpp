#include "ixml.h"

namespace wmbus {

namespace {

// Backtracking, longest repetition first. No recursion limits or index bounds:
// a grammar is part of a driver, not data off the radio.
constexpr size_t NO_MATCH = static_cast<size_t>(-1);

// The bytes no deleted term consumed; `start` is NO_MATCH until the first such term.
struct Content {
  size_t start = NO_MATCH;
  size_t end = 0;

  size_t from(size_t fallback) const { return start == NO_MATCH ? fallback : start; }
};

struct SequenceMatch {
  size_t end;
  Content content;
};

struct Matcher {
  const IxmlGrammar &grammar;
  std::span<const uint8_t> input;
  std::vector<IxmlMatch> &matches;

  Matcher(const IxmlGrammar &g, std::span<const uint8_t> in, std::vector<IxmlMatch> &out)
      : grammar(g), input(in), matches(out) {}

  size_t match_pattern(const IxmlTerm &term, size_t position) const {
    if (position + term.length > input.size())
      return NO_MATCH;

    for (uint16_t i = 0; i < term.length; ++i) {
      const IxmlBytePattern &pattern = grammar.patterns[term.arg + i];
      const uint8_t byte = input[position + i];
      if ((pattern.high & (1u << (byte >> 4))) == 0)
        return NO_MATCH;
      if ((pattern.low & (1u << (byte & 0x0F))) == 0)
        return NO_MATCH;
    }
    return position + term.length;
  }

  std::span<const uint8_t> key_of(const IxmlRule &rule) const {
    return grammar.keys.subspan(rule.first_key_byte, rule.key_length);
  }

  size_t match_once(const IxmlTerm &term, size_t position) {
    if (term.kind == IxmlTermKind::Pattern)
      return match_pattern(term, position);
    return match_rule(term.arg, position);
  }

  SequenceMatch match_sequence(const IxmlAlternative &alternative, uint16_t term_index, size_t position,
                               Content content) {
    if (term_index == alternative.term_count)
      return {position, content};

    const IxmlTerm &term = grammar.terms[alternative.first_term + term_index];
    const size_t emitted = matches.size();

    const bool many = term.repeat == IxmlRepeat::Star || term.repeat == IxmlRepeat::Plus;
    const size_t min_repeats = (term.repeat == IxmlRepeat::Once || term.repeat == IxmlRepeat::Plus) ? 1 : 0;

    size_t available = 0;
    size_t at = position;
    while (many || available == 0) {
      const size_t next = match_once(term, at);
      if (next == NO_MATCH)
        break;
      ++available;
      // A rule may match the empty string; repeating it would not move forward.
      if (next == at)
        break;
      at = next;
    }

    for (size_t taken = available + 1; taken-- > min_repeats;) {
      // Drop what the longer attempts emitted.
      matches.resize(emitted);

      Content taken_content = content;
      size_t cursor = position;
      for (size_t i = 0; i < taken; ++i)
        cursor = match_once(term, cursor);

      if (taken > 0 && !term.deleted) {
        taken_content.start = taken_content.from(position);
        taken_content.end = cursor;
      }

      const SequenceMatch rest = match_sequence(alternative, term_index + 1, cursor, taken_content);
      if (rest.end != NO_MATCH)
        return rest;
    }

    matches.resize(emitted);
    return {NO_MATCH, content};
  }

  // A later alternative may be the one that covers the whole input.
  bool match_whole_input(uint16_t rule_index) {
    const IxmlRule &rule = grammar.rules[rule_index];
    for (uint16_t i = 0; i < rule.alternative_count; ++i) {
      matches.clear();

      const SequenceMatch m = match_sequence(grammar.alternatives[rule.first_alternative + i], 0, 0, {});
      if (m.end != input.size())
        continue;

      if (rule.key_length != 0) {
        const size_t from = m.content.from(0);
        matches.push_back(IxmlMatch{key_of(rule), input.subspan(from, m.content.end - from)});
      }
      return true;
    }

    matches.clear();
    return false;
  }

  size_t match_rule(uint16_t rule_index, size_t position) {
    const IxmlRule &rule = grammar.rules[rule_index];
    const size_t emitted = matches.size();

    for (uint16_t i = 0; i < rule.alternative_count; ++i) {
      const IxmlAlternative &alternative = grammar.alternatives[rule.first_alternative + i];

      const SequenceMatch m = match_sequence(alternative, 0, position, {NO_MATCH, position});
      if (m.end == NO_MATCH) {
        matches.resize(emitted);
        continue;
      }

      if (rule.key_length != 0) {
        const size_t from = m.content.from(position);
        matches.push_back(IxmlMatch{key_of(rule), input.subspan(from, m.content.end - from)});
      }
      return m.end;
    }

    matches.resize(emitted);
    return NO_MATCH;
  }
};

}  // namespace

std::optional<std::vector<IxmlMatch>> IxmlGrammar::decode(std::span<const uint8_t> bytes) const {
  std::vector<IxmlMatch> collected;
  Matcher matcher(*this, bytes, collected);
  if (!matcher.match_whole_input(0))
    return {};
  return collected;
}

}  // namespace wmbus
