#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wmbus {

enum class IxmlTermKind : uint8_t { Pattern, Rule };

enum class IxmlRepeat : uint8_t { Once, Star, Plus, Optional };

// Every value one half of a byte may take.
constexpr uint16_t IXML_ANY = 0xFFFF;

// A bit per value the high and the low nibble may take.
struct IxmlBytePattern {
  uint16_t high;
  uint16_t low;
};

struct IxmlTerm {
  IxmlTermKind kind;
  IxmlRepeat repeat;
  bool deleted;    // a deleted term does not enter the node's content
  uint8_t length;  // pattern bytes; 0 for a rule
  uint16_t arg;    // the pattern's first byte, or the index of the rule called
};

struct IxmlAlternative {
  uint16_t first_term;
  uint16_t term_count;
};

struct IxmlRule {
  uint16_t first_alternative;
  uint16_t alternative_count;
  uint16_t first_key_byte;
  uint16_t key_length;  // 0 when the rule carries no DIF/VIF key
};

struct IxmlMatch {
  std::span<const uint8_t> key;
  std::span<const uint8_t> data;
};

struct IxmlGrammar {
  // Rule 0 is the start rule.
  std::span<const IxmlRule> rules;
  std::span<const IxmlAlternative> alternatives;
  std::span<const IxmlTerm> terms;
  std::span<const IxmlBytePattern> patterns;
  std::span<const uint8_t> keys;

  // Nothing unless the start rule covers the whole input; empty when no key was recognised.
  std::optional<std::vector<IxmlMatch>> decode(std::span<const uint8_t> bytes) const;
};

}  // namespace wmbus
