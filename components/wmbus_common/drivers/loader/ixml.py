"""Compiles driver ixml grammars to tables at codegen time.

Grammars match a frame's hex transcript, the device its bytes: character runs become
byte patterns, one pair of nibble masks per byte.
"""

import re
from dataclasses import dataclass, field
from enum import StrEnum
from typing import ClassVar

from esphome.core import HexInt
from esphome.cpp_generator import Expression, literal

from .cpp import Aggregate, Tables, wmbus_ns


class Repeat(StrEnum):
    Once = ""
    Star = "*"
    Plus = "+"
    Optional = "?"


@dataclass(kw_only=True)
class Term:
    deleted: bool = False
    attribute: bool = False
    repeat: Repeat = Repeat.Once


@dataclass
class Literal(Term):
    text: str


@dataclass
class Insert(Term):
    text: str


@dataclass
class RuleRef(Term):
    name: str


@dataclass
class CharClass(Term):
    ranges: list[tuple[str, str]]


@dataclass
class Group(Term):
    alternatives: list[list[Term]]


@dataclass
class Rule:
    name: str
    alternatives: list[list[Term]] = field(default_factory=list)
    alias: str | None = None  # "DV_0413>dvk": the node is named dvk


@dataclass(frozen=True)
class Token:
    text: str
    pattern: ClassVar[re.Pattern]

    @classmethod
    def at(cls, text: str, position: int) -> "Token":
        for kind in cls.__subclasses__():
            if match := kind.pattern.match(text, position):
                return kind(match.group())


class Blank(Token):
    pattern = re.compile(r"\s+|\{[^{}]*\}")


class Quoted(Token):
    pattern = re.compile(r"'[^']*'")


class Bracketed(Token):
    pattern = re.compile(r"\[[^\]]*\]")


class Name(Token):
    pattern = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class Punct(Token):
    pattern = re.compile(r"[-@^+*?|,.=()>]")


def tokenize(text: str):
    position = 0
    while position < len(text):
        token = Token.at(text, position)
        position += len(token.text)
        if not isinstance(token, Blank):
            yield token


class _Parser:
    def __init__(self, text: str):
        self.tokens = list(tokenize(text))
        self.pos = 0

    def peek(self) -> Token | None:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def take(self) -> Token:
        token = self.peek()
        self.pos += 1
        return token

    def parse(self) -> dict[str, Rule]:
        rules: dict[str, Rule] = {}
        while self.peek() is not None:
            rule = self.parse_rule()
            rules[rule.name] = rule
        return rules

    def parse_rule(self) -> Rule:
        # A "-" rule hides its node, which is irrelevant: only the keys matter.
        if self.peek() == Punct("-"):
            self.take()

        name = self.take().text

        alias = None
        if self.peek() == Punct(">"):
            self.take()
            alias = self.take().text

        self.take()  # =
        alternatives = self.parse_alternatives()
        self.take()  # .

        return Rule(name=name, alternatives=alternatives, alias=alias)

    def parse_alternatives(self) -> list[list[Term]]:
        alternatives = [self.parse_sequence()]
        while self.peek() == Punct("|"):
            self.take()
            alternatives.append(self.parse_sequence())
        return alternatives

    def parse_sequence(self) -> list[Term]:
        terms = [self.parse_term()]
        while self.peek() == Punct(","):
            self.take()
            terms.append(self.parse_term())
        return terms

    def parse_term(self) -> Term:
        deleted = attribute = inserted = False

        while True:
            match self.peek():
                case Punct("-"):
                    deleted = True
                case Punct("@"):
                    attribute = True
                case Punct("+"):
                    inserted = True
                case Punct("^"):
                    pass  # show despite a hidden rule: makes no difference here
                case _:
                    break
            self.take()

        match self.take():
            case Quoted(text) if inserted:
                term = Insert(text[1:-1])
            case Quoted(text):
                term = Literal(text[1:-1])
            case Name(text):
                term = RuleRef(text)
            case Bracketed(text):
                term = CharClass(parse_char_class(text))
            case Punct("("):
                term = Group(self.parse_alternatives())
                self.take()  # )

        term.deleted = deleted
        term.attribute = attribute

        match self.peek():
            case Punct(mark) if mark in Repeat:
                self.take()
                term.repeat = Repeat(mark)

        return term


def parse_char_class(text: str) -> list[tuple[str, str]]:
    ranges = []
    for part in text[1:-1].split(";"):
        part = part.strip()
        if not part:
            continue
        if match := re.fullmatch(r"'(.)'\s*-\s*'(.)'", part):
            ranges.append((match.group(1), match.group(2)))
        else:
            ranges.extend((c, c) for c in part[1:-1])

    return ranges


def walk_terms(alternatives):
    for sequence in alternatives:
        for term in sequence:
            yield term
            if isinstance(term, Group):
                yield from walk_terms(term.alternatives)


class TermKind(StrEnum):
    Pattern = "Pattern"
    Rule = "Rule"


START_RULE = "decode"

ANY_NIBBLE = 0xFFFF


def class_mask(ranges: list[tuple[str, str]]) -> int:
    values = {value for first, last in ranges for value in range(int(first, 16), int(last, 16) + 1)}
    return sum(1 << value for value in values)


@dataclass(kw_only=True)
class NormTerm:
    repeat: Repeat
    deleted: bool


@dataclass
class NormPattern(NormTerm):
    nibbles: list[int]


@dataclass
class NormRule(NormTerm):
    rule: int


@dataclass
class CompiledTerm:
    kind: TermKind
    arg: int  # first pattern byte, or rule index
    length: int
    deleted: bool
    repeat: Repeat


@dataclass
class CompiledRule:
    name: str
    key: bytes | None
    alternatives: list[list[CompiledTerm]]


class Compiler:
    def __init__(self, rules: dict[str, Rule]):
        self.source = dict(rules)
        self.rules: list[CompiledRule] = []
        self.index_of: dict[str, int] = {}
        self.patterns: list[tuple[int, int]] = []
        self.pattern_at: dict[tuple, int] = {}
        self.keys = bytearray()

        # @X always points at a rule of the form  X>dvk = +'literal'.
        self.attribute_keys = {
            name: bytes.fromhex(rule.alternatives[0][0].text)
            for name, rule in rules.items()
            if rule.alias == "dvk"
        }

    def intern_pattern(self, nibbles: list[int]) -> tuple[int, int]:
        pattern = tuple(zip(nibbles[0::2], nibbles[1::2]))
        if pattern not in self.pattern_at:
            self.pattern_at[pattern] = len(self.patterns)
            self.patterns.extend(pattern)
        return self.pattern_at[pattern], len(pattern)

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

    def fixed_nibbles(self, term: Term, visiting: tuple = ()) -> list[int] | None:
        if term.attribute:
            return None

        if isinstance(term, Literal):
            return [1 << int(char, 16) for char in term.text]

        if isinstance(term, CharClass):
            return [class_mask(term.ranges)]

        alternatives = self.body_of(term, visiting)
        if alternatives is None or len(alternatives) != 1:
            return None

        nibbles = []
        for inner in alternatives[0]:
            if inner.repeat is not Repeat.Once:
                return None
            inner_nibbles = self.fixed_nibbles(inner, visiting + (self.name_of(term),))
            if inner_nibbles is None:
                return None
            nibbles.extend(inner_nibbles)
        return nibbles

    def name_of(self, term: Term) -> str | None:
        return term.name if isinstance(term, RuleRef) else None

    def body_of(self, term: Term, visiting: tuple) -> list[list[Term]] | None:
        if isinstance(term, Group):
            return term.alternatives
        if not isinstance(term, RuleRef) or term.name in visiting:
            return None

        rule = self.source[term.name]
        if rule.alias is not None:
            return None
        if any(inner.attribute for inner in walk_terms(rule.alternatives)):
            return None
        return rule.alternatives

    def expand(self, term: Term, deleted: bool, owner: str, pending: list[str],
               visiting: tuple = ()) -> list[NormTerm]:
        nibbles = self.fixed_nibbles(term, visiting)
        if nibbles is not None:
            return [NormPattern(repeat=term.repeat, deleted=deleted,
                                nibbles=self.repeatable(nibbles, term.repeat))]

        # Key-less helpers ('byte', 'word', 'hdr') are inlined, taking the caller's mark.
        body = self.body_of(term, visiting) if term.repeat is Repeat.Once else None
        if body is not None and len(body) == 1:
            inlined = []
            for inner in body[0]:
                inlined.extend(self.expand(inner, deleted, owner, pending,
                                           visiting + (self.name_of(term),)))
            return inlined

        return [NormRule(repeat=term.repeat, deleted=deleted,
                         rule=self.reference(term, owner, pending))]

    def reference(self, term: Term, owner: str, pending: list[str]) -> int:
        match term:
            case RuleRef(name=name):
                pending.append(name)
                return self.reserve(name)

            case Group(alternatives=alternatives):
                name = f"{owner}__group{len(self.rules)}"
                self.source[name] = Rule(name=name, alternatives=alternatives)
                pending.append(name)
                return self.reserve(name)

    def repeatable(self, nibbles: list[int], repeat: Repeat) -> list[int]:
        if repeat is Repeat.Once or len(nibbles) % 2 == 0:
            return nibbles
        # 'hex*' in a byte-aligned grammar only matches an even count, so repeat the pair.
        return nibbles + nibbles

    def pack(self, terms: list[NormTerm]) -> list[NormTerm]:
        packed: list[NormTerm] = []
        for term in terms:
            previous = packed[-1] if packed else None
            joinable = (isinstance(previous, NormPattern) and isinstance(term, NormPattern)
                        and previous.repeat is Repeat.Once and term.repeat is Repeat.Once
                        and previous.deleted == term.deleted)
            if joinable:
                previous.nibbles.extend(term.nibbles)
                continue
            packed.append(term)
        return packed

    def compile(self) -> list[CompiledRule]:
        self.reserve(START_RULE)
        pending = [START_RULE]
        seen = set()

        while pending:
            name = pending.pop(0)
            if name in seen:
                continue
            seen.add(name)

            key = None
            alternatives = []
            for sequence in self.source[name].alternatives:
                terms = []
                for term in sequence:
                    if term.attribute:
                        key = self.attribute_keys[term.name]
                        continue
                    terms.extend(self.expand(term, term.deleted, name, pending))
                alternatives.append([self.settle(t) for t in self.pack(terms)])

            self.rules[self.index_of[name]] = CompiledRule(name, key, alternatives)
            if key is not None:
                self.intern_key(key)

        return self.rules

    def settle(self, term: NormTerm) -> CompiledTerm:
        match term:
            case NormRule(rule=rule):
                return CompiledTerm(TermKind.Rule, rule, 0, term.deleted, term.repeat)

            case NormPattern(nibbles=nibbles):
                first, length = self.intern_pattern(nibbles)
                return CompiledTerm(TermKind.Pattern, first, length, term.deleted, term.repeat)


def compile_grammar(text: str) -> Compiler:
    compiler = Compiler(_Parser(text).parse())
    compiler.compile()
    return compiler


TERM_KIND = wmbus_ns.enum("IxmlTermKind", is_class=True)
REPEAT = wmbus_ns.enum("IxmlRepeat", is_class=True)


def nibble_values(mask: int) -> Expression:
    return wmbus_ns.IXML_ANY if mask == ANY_NIBBLE else literal(f"0x{mask:04X}")


def emit_tables(compiler: Compiler, tables: Tables, prefix: str) -> Expression:
    patterns = [
        Aggregate(("high", nibble_values(high)), ("low", nibble_values(low)))
        for high, low in compiler.patterns
    ]
    keys = [HexInt(byte) for byte in compiler.keys]

    terms: list[Expression] = []
    alternatives: list[Expression] = []
    rules: list[Expression] = []

    for rule in compiler.rules:
        first_alternative = len(alternatives)
        for sequence in rule.alternatives:
            first_term = len(terms)
            for term in sequence:
                terms.append(Aggregate(
                    ("kind", getattr(TERM_KIND, term.kind)),
                    ("repeat", getattr(REPEAT, term.repeat.name)),
                    ("deleted", term.deleted),
                    ("length", term.length),
                    ("arg", term.arg),
                ))
            alternatives.append(
                Aggregate(("first_term", first_term), ("term_count", len(sequence)))
            )

        first_key, key_length = compiler.intern_key(rule.key) if rule.key else (0, 0)
        rules.append(Aggregate(
            ("first_alternative", first_alternative),
            ("alternative_count", len(rule.alternatives)),
            ("first_key_byte", first_key),
            ("key_length", key_length),
        ))

    grammar = Aggregate(
        ("rules", tables.span("IxmlRule", f"{prefix}_rules", rules)),
        ("alternatives", tables.span("IxmlAlternative", f"{prefix}_alternatives", alternatives)),
        ("terms", tables.span("IxmlTerm", f"{prefix}_terms", terms)),
        ("patterns", tables.span("IxmlBytePattern", f"{prefix}_patterns", patterns)),
        ("keys", tables.span("uint8_t", f"{prefix}_keys", keys)),
    )
    return tables.define("IxmlGrammar", f"{prefix}_grammar", grammar)


def compile_to_tables(text: str, tables: Tables, prefix: str) -> Expression:
    return emit_tables(compile_grammar(text), tables, prefix)
