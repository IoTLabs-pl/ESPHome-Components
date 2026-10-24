"""Compiles driver ixml grammars to tables at codegen time.

Grammars match a frame's hex transcript, the device its bytes: character runs become
byte patterns, one pair of nibble masks per byte.
"""

import re
from dataclasses import dataclass

from esphome.core import HexInt
from esphome.cpp_generator import Expression, literal

from .cpp import Aggregate, Tables, enumerator, wmbus_ns

TOKEN = re.compile(r"\s+|\{[^{}]*\}|('[^']*'|\[[^\]]*\]|[A-Za-z_][A-Za-z0-9_]*|[-@^+*?|,.=()>])")
REPEATS = {"": "Once", "*": "Star", "+": "Plus", "?": "Optional"}
START_RULE = "decode"
ANY_NIBBLE = 0xFFFF


@dataclass
class Term:
    kind: str  # literal, insert, rule, class or group
    value: object  # the text, the rule's name, the [class] or the alternatives
    deleted: bool = False
    attribute: bool = False
    repeat: str = ""


@dataclass
class Rule:
    alternatives: list[list[Term]]
    alias: str | None = None  # "DV_0413>dvk": the node is named dvk


def parse_grammar(text: str) -> dict[str, Rule]:
    tokens, pos = [], 0
    while pos < len(text):
        m = TOKEN.match(text, pos)
        pos = m.end()
        if m[1]:
            tokens.append(m[1])
    tokens.reverse()

    def peek() -> str | None:
        return tokens[-1] if tokens else None

    def take() -> str | None:
        return tokens.pop() if tokens else None

    def alternatives() -> list[list[Term]]:
        result = [sequence()]
        while peek() == "|":
            take()
            result.append(sequence())
        return result

    def sequence() -> list[Term]:
        result = [term()]
        while peek() == ",":
            take()
            result.append(term())
        return result

    def term() -> Term:
        # ^ shows a node despite a hidden rule, which makes no difference here.
        marks = set()
        while peek() in ("-", "@", "+", "^"):
            marks.add(take())
        token = take()
        if token == "(":
            term = Term("group", alternatives())
            take()  # )
        elif token[0] == "'":
            term = Term("insert" if "+" in marks else "literal", token[1:-1])
        elif token[0] == "[":
            term = Term("class", token)
        elif token[0].isalpha() or token[0] == "_":
            term = Term("rule", token)
        else:
            raise ValueError(f"ixml: unexpected {token!r}")
        term.deleted, term.attribute = "-" in marks, "@" in marks
        if peek() in ("*", "+", "?"):
            term.repeat = take()
        return term

    rules = {}
    while peek() is not None:
        # A "-" rule hides its node, which is irrelevant: only the keys matter.
        if peek() == "-":
            take()
        name = take()
        alias = None
        if peek() == ">":
            take()
            alias = take()
        take()  # =
        rule = Rule(alternatives(), alias)
        take()  # .
        rules[name] = rule
    return rules


def class_mask(text: str) -> int:
    """['A'-'F';'0'-'9'] as one bit per nibble value."""
    values = set()
    for part in text[1:-1].split(";"):
        part = part.strip()
        if match := re.fullmatch(r"'(.)'\s*-\s*'(.)'", part):
            values.update(range(int(match[1], 16), int(match[2], 16) + 1))
        elif part:
            values.update(int(char, 16) for char in part[1:-1])
    return sum(1 << value for value in values)


@dataclass
class Piece:
    """A run of nibble masks, or a rule by index."""

    repeat: str
    deleted: bool
    nibbles: list[int] | None = None
    rule: int | None = None


class Compiler:
    def __init__(self, rules: dict[str, Rule]):
        self.source = dict(rules)
        self.rules: list = []  # (key, alternatives of (kind, arg, length, deleted, repeat))
        self.index_of: dict[str, int] = {}
        self.patterns: list[tuple[int, int]] = []
        self.pattern_at: dict[tuple, int] = {}
        self.keys = bytearray()
        # @X always points at a rule of the form  X>dvk = +'literal'.
        self.attribute_keys = {
            name: bytes.fromhex(rule.alternatives[0][0].value) for name, rule in rules.items() if rule.alias == "dvk"
        }

    def intern_key(self, key: bytes) -> tuple[int, int]:
        at = self.keys.find(key)
        if at < 0:
            at = len(self.keys)
            self.keys.extend(key)
        return at, len(key)

    def reserve(self, name: str) -> int:
        if name not in self.index_of:
            self.index_of[name] = len(self.rules)
            self.rules.append(None)
        return self.index_of[name]

    def body_of(self, term: Term, visiting: tuple) -> list[list[Term]] | None:
        """The alternatives a group or a key-less rule stands for."""
        if term.kind == "group":
            return term.value
        if term.kind != "rule" or term.value in visiting:
            return None
        rule = self.source[term.value]
        if rule.alias is not None or any(inner.attribute for inner in walk(rule.alternatives)):
            return None
        return rule.alternatives

    def fixed_nibbles(self, term: Term, visiting: tuple = ()) -> list[int] | None:
        if term.attribute:
            return None
        if term.kind == "literal":
            return [1 << int(char, 16) for char in term.value]
        if term.kind == "class":
            return [class_mask(term.value)]
        body = self.body_of(term, visiting)
        if body is None or len(body) != 1:
            return None
        nibbles = []
        for inner in body[0]:
            inner_nibbles = None if inner.repeat else self.fixed_nibbles(inner, inside(visiting, term))
            if inner_nibbles is None:
                return None
            nibbles += inner_nibbles
        return nibbles

    def expand(self, term: Term, deleted: bool, owner: str, pending: list[str], visiting: tuple = ()) -> list[Piece]:
        nibbles = self.fixed_nibbles(term, visiting)
        if nibbles is not None:
            if term.repeat and len(nibbles) % 2:
                # 'hex*' in a byte-aligned grammar only matches an even count, so repeat the pair.
                nibbles += nibbles
            return [Piece(term.repeat, deleted, nibbles=nibbles)]

        # Key-less helpers ('byte', 'word', 'hdr') are inlined, taking the caller's mark.
        body = None if term.repeat else self.body_of(term, visiting)
        if body is not None and len(body) == 1:
            return [p for inner in body[0] for p in self.expand(inner, deleted, owner, pending, inside(visiting, term))]

        name = term.value
        if term.kind == "group":
            name = f"{owner}__group{len(self.rules)}"
            self.source[name] = Rule(term.value)
        elif term.kind != "rule":
            return [Piece(term.repeat, deleted)]
        pending.append(name)
        return [Piece(term.repeat, deleted, rule=self.reserve(name))]

    def compile(self) -> None:
        self.reserve(START_RULE)
        pending, seen = [START_RULE], set()
        while pending:
            name = pending.pop(0)
            if name in seen:
                continue
            seen.add(name)

            key = None
            alternatives = []
            for sequence in self.source[name].alternatives:
                pieces: list[Piece] = []
                for term in sequence:
                    if term.attribute:
                        key = self.attribute_keys[term.value]
                        continue
                    for piece in self.expand(term, term.deleted, name, pending):
                        last = pieces[-1] if pieces else None
                        if (last and last.nibbles is not None and piece.nibbles is not None
                                and not last.repeat and not piece.repeat and last.deleted == piece.deleted):
                            last.nibbles += piece.nibbles
                        else:
                            pieces.append(piece)
                alternatives.append([self.settle(p) for p in pieces])

            self.rules[self.index_of[name]] = (key, alternatives)
            if key is not None:
                self.intern_key(key)

    def settle(self, piece: Piece) -> tuple:
        if piece.nibbles is None:
            return "Rule", piece.rule, 0, piece.deleted, piece.repeat
        pattern = tuple(zip(piece.nibbles[0::2], piece.nibbles[1::2]))
        if pattern not in self.pattern_at:
            self.pattern_at[pattern] = len(self.patterns)
            self.patterns.extend(pattern)
        return "Pattern", self.pattern_at[pattern], len(pattern), piece.deleted, piece.repeat


def inside(visiting: tuple, term: Term) -> tuple:
    """The rules being inlined, so that a recursive one is referenced instead."""
    return (*visiting, term.value) if term.kind == "rule" else visiting


def walk(alternatives):
    for sequence in alternatives:
        for term in sequence:
            yield term
            if term.kind == "group":
                yield from walk(term.value)


def nibble_values(mask: int) -> Expression:
    return wmbus_ns.IXML_ANY if mask == ANY_NIBBLE else literal(f"0x{mask:04X}")


def compile_to_tables(text: str, tables: Tables, prefix: str) -> Expression:
    compiler = Compiler(parse_grammar(text))
    compiler.compile()

    terms, alternatives, rules = [], [], []
    for key, sequences in compiler.rules:
        first_alternative = len(alternatives)
        for sequence in sequences:
            alternatives.append(Aggregate(("first_term", len(terms)), ("term_count", len(sequence))))
            terms += [
                Aggregate(
                    ("kind", enumerator("IxmlTermKind", kind)),
                    ("repeat", enumerator("IxmlRepeat", REPEATS[repeat])),
                    ("deleted", deleted),
                    ("length", length),
                    ("arg", arg),
                )
                for kind, arg, length, deleted, repeat in sequence
            ]
        first_key, key_length = compiler.intern_key(key) if key else (0, 0)
        rules.append(Aggregate(
            ("first_alternative", first_alternative),
            ("alternative_count", len(sequences)),
            ("first_key_byte", first_key),
            ("key_length", key_length),
        ))

    patterns = [Aggregate(("high", nibble_values(hi)), ("low", nibble_values(lo))) for hi, lo in compiler.patterns]
    grammar = Aggregate(
        ("rules", tables.span("IxmlRule", f"{prefix}_rules", rules)),
        ("alternatives", tables.span("IxmlAlternative", f"{prefix}_alternatives", alternatives)),
        ("terms", tables.span("IxmlTerm", f"{prefix}_terms", terms)),
        ("patterns", tables.span("IxmlBytePattern", f"{prefix}_patterns", patterns)),
        ("keys", tables.span("uint8_t", f"{prefix}_keys", [HexInt(byte) for byte in compiler.keys])),
    )
    return tables.define("IxmlGrammar", f"{prefix}_grammar", grammar)
