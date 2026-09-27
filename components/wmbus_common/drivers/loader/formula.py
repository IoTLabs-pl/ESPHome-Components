"""Compiles XMQ `calculate` formulas to C++ at codegen, keeping upstream's quirks.

  * no precedence: left to right, a binary operator takes one operand on its right,
  * + and - convert the left side to the right side's unit,
  * counter operators (%, >>, ==, &, ...) and round/floor/ceil ignore the target unit,
  * a date literal (mktime in the device's zone) and adding months stay run-time calls.

Unsupported because no driver uses them: **, time literals, temperature scale
conversion, adding years to a date. Upstream has parsed every formula already, so
only these raise; the syntax and the field references are taken as valid.
"""

from __future__ import annotations

import math
import operator
import re
from dataclasses import dataclass
from enum import Enum, StrEnum
from typing import Callable

from esphome.cpp_generator import Expression, MockObj, literal
from esphome.cpp_types import std_ns

from .cpp import bin_op, cast, double, ternary, wmbus_ns
from .units import BY_SYMBOL, UNITS, Quantity, SIExp, Unit, split_unit


@dataclass(frozen=True)
class SIUnit:
    quantity: Quantity
    scale: float
    exp: SIExp

    # A product or a quotient never lands on a named unit, so its quantity is Unknown.
    def mul(self, other: "SIUnit") -> "SIUnit":
        return SIUnit(Quantity.Unknown, self.scale * other.scale, self.exp.mul(other.exp))

    def div(self, other: "SIUnit") -> "SIUnit":
        return SIUnit(Quantity.Unknown, self.scale / other.scale, self.exp.div(other.exp))

    def sqrt(self) -> "SIUnit":
        return SIUnit(Quantity.Unknown, math.sqrt(self.scale), self.exp.sqrt())


def si_unit(unit: Unit) -> SIUnit:
    """A unit without SI terms (TXT, DateTimeLT) converts to nothing."""
    info = UNITS[unit]
    if info.si is None:
        return SIUnit(Quantity.Unknown, 0.0, SIExp())
    return SIUnit(info.quantity, *info.si)


ZERO = double(0.0)
ONE = double(1.0)

Emit = Callable[..., Expression]


def _truth(spelling: str) -> tuple[str, int, Emit]:
    return (
        spelling,
        2,
        lambda left, right: ternary(bin_op(left != ZERO, spelling, right != ZERO), ONE, ZERO),
    )


def _comparison(spelling: str, compare: Callable) -> tuple[str, int, Emit]:
    return spelling, 2, lambda left, right: ternary(compare(left, right), ONE, ZERO)


class Op(Enum):
    ADD = ("+", 2, operator.add)
    SUB = ("-", 2, operator.sub)
    MUL = ("*", 2, operator.mul)
    DIV = ("/", 2, operator.truediv)
    MOD = ("%", 2, wmbus_ns.modulo)
    SHL = ("<<", 2, wmbus_ns.shift_left)
    SHR = (">>", 2, wmbus_ns.shift_right)
    BAND = ("&", 2, wmbus_ns.bit_and)
    BOR = ("|", 2, wmbus_ns.bit_or)
    BXOR = ("^", 2, wmbus_ns.bit_xor)
    LAND = _truth("&&")
    LOR = _truth("||")
    EQ = _comparison("==", operator.eq)
    NEQ = _comparison("!=", operator.ne)
    LT = _comparison("<", operator.lt)
    GT = _comparison(">", operator.gt)
    LTE = _comparison("<=", operator.le)
    GTE = _comparison(">=", operator.ge)
    SQRT = ("sqrt", 1, std_ns.sqrt)
    ROUND = ("round", 1, std_ns.round)
    FLOOR = ("floor", 1, std_ns.floor)
    CEIL = ("ceil", 1, std_ns.ceil)

    def __init__(self, spelling: str, arity: int, emit: Emit):
        self.spelling = spelling
        self.arity = arity
        self.emit = emit


class Tok(Enum):
    NUMBER = "number"
    FIELD = "field"
    UNIT = "unit"
    DATETIME = "datetime"
    LPAR = "("
    RPAR = ")"
    COMMA = ","
    MKDATE = "mkdate"


def conversion(src: SIUnit, dst: SIUnit) -> tuple[float, float] | None:
    """(from scale, to scale): a value converts as v * from / to."""
    if src.exp.exps != dst.exp.exps:
        return None
    return src.scale, dst.scale


def _unix_timestamp_exp() -> SIExp:
    return UNITS[Unit.UnixTimestamp].si[1]


def _is_second(exp: SIExp) -> bool:
    return exp == UNITS[Unit.Second].si[1]


def math_op_unit(op: Op, left: SIUnit, right: SIUnit) -> SIUnit | None:
    ut = _unix_timestamp_exp()
    if left.exp == right.exp:
        if op is Op.ADD and left.exp == ut:
            return None
        return right
    if left.exp == ut or right.exp == ut:
        if right.exp == ut:
            return math_op_unit(op, right, left)
        if _is_second(right.exp) or right.exp == UNITS[Unit.Month].si[1]:
            return si_unit(Unit.UnixTimestamp)
    return None


_DATETIME_FORMATS = (
    (21, r"'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}'"),
    (12, r"'\d{4}-\d{2}-\d{2}'"),
)

# Longest spelling first, so that >> wins over >.
_FIXED_TOKENS = tuple(
    sorted(
        [(op.spelling, op) for op in Op]
        + [(tok.value, tok) for tok in (Tok.LPAR, Tok.RPAR, Tok.COMMA, Tok.MKDATE)],
        key=lambda pair: len(pair[0]),
        reverse=True,
    )
)


@dataclass(frozen=True)
class Token:
    kind: Tok | Op
    text: str


def _is_letter(c: str) -> bool:
    return "a" <= c <= "z"


def _is_letter_digit_or_underscore(c: str) -> bool:
    return c == "_" or "a" <= c <= "z" or "0" <= c <= "9"


def tokenize(f: str) -> list[Token]:
    tokens = []
    i = 0
    n = len(f)
    lcnames = sorted(BY_SYMBOL, key=len, reverse=True)

    while i < n:
        c = f[i]
        if c.isspace():
            i += 1
            continue

        matched = False
        for length, pattern in _DATETIME_FORMATS:
            if i + length - 1 < n and re.match(pattern, f[i : i + length]):
                tokens.append(Token(Tok.DATETIME, f[i : i + length]))
                i += length
                matched = True
                break
        if matched:
            continue

        m = re.match(r"[0-9][0-9.]*", f[i:])
        if m and m.group(0).count(".") <= 1:
            tokens.append(Token(Tok.NUMBER, m.group(0)))
            i += len(m.group(0))
            continue

        for text, kind in _FIXED_TOKENS:
            if not f.startswith(text, i):
                continue
            # Upstream wants a character after a spelled-out operator, so one cannot end a formula.
            if text.isalpha() and (i + len(text) >= n or _is_letter(f[i + len(text)])):
                continue
            tokens.append(Token(kind, text))
            i += len(text)
            matched = True
            break
        if matched:
            continue

        if _is_letter(c):
            # Upstream tries units before fields.
            for lc in lcnames:
                end = i + len(lc)
                if f.startswith(lc, i) and (end >= n or not _is_letter_digit_or_underscore(f[end])):
                    tokens.append(Token(Tok.UNIT, lc))
                    i = end
                    matched = True
                    break
            if matched:
                continue

        m = re.match(r"[a-z][a-z0-9_]*", f[i:])
        tokens.append(Token(Tok.FIELD, m.group(0)))
        i += len(m.group(0))

    return tokens


class Counter(StrEnum):
    """Values are DVEntryCounterType enumerators; formulas name them in lower case."""

    STORAGE_COUNTER = "STORAGE_COUNTER"
    TARIFF_COUNTER = "TARIFF_COUNTER"
    SUBUNIT_COUNTER = "SUBUNIT_COUNTER"

    @staticmethod
    def named(field_name: str) -> "Counter | None":
        return next((c for c in Counter if c.lower() == field_name), None)

    @property
    def prefix(self) -> str:
        """As in storage_from, match_tariff_nr."""
        return self.lower().removesuffix("_counter")


@dataclass
class Node:
    si: SIUnit


@dataclass
class Constant(Node):
    value: float


@dataclass
class DateTimeLiteral(Node):
    parts: tuple[int, int, int, int, int, int]


@dataclass
class MeterField(Node):
    vname: str
    slot: int
    display_unit: Unit


@dataclass
class CounterField(Node):
    counter: Counter


@dataclass
class Binary(Node):
    op: Op
    left: Node
    right: Node


@dataclass
class Unary(Node):
    op: Op
    inner: Node


@dataclass
class MkDate(Node):
    year: Node
    month: Node
    day: Node


@dataclass(frozen=True)
class FieldRef:
    slot: int
    display_unit: Unit


# (vname, the unit the formula names it in) -> the field it refers to.
ResolveField = Callable[[str, Unit], FieldRef]


class Parser:
    def __init__(self, formula: str, resolve_field: ResolveField):
        self.tokens = tokenize(formula)
        self.resolve_field = resolve_field
        self.stack: list[Node] = []

    def la(self, i: int) -> Token | None:
        return self.tokens[i] if i < len(self.tokens) else None

    def parse(self) -> Node:
        i = 0
        while (nxt := self.parse_ops(i)) != i:
            i = nxt
        return self.stack.pop()

    def parse_ops(self, i: int) -> int:
        tok = self.la(i)
        if tok is None:
            return i

        match tok.kind:
            case Tok.FIELD:
                self.handle_field(tok)
                return i + 1

            case Tok.DATETIME:
                self.stack.append(DateTimeLiteral(si_unit(Unit.UnixTimestamp), self.datetime_parts(tok.text)))
                return i + 1

            case Op(arity=2):
                after = self.parse_ops(i + 1)
                self.handle_binary(tok)
                return after

            case Op(arity=1):
                after = self.parse_ops(i + 1)
                self.handle_unary(tok)
                return after

            case Tok.MKDATE:
                return self.parse_mkdate(i)

            case Tok.LPAR:
                return self.parse_par(i)

            case Tok.NUMBER:
                unit = BY_SYMBOL[self.tokens[i + 1].text]
                self.stack.append(Constant(si_unit(unit), float(tok.text)))
                return i + 2

            # A comma, a closing parenthesis or a bare unit is for the caller to read.
            case _:
                return i

    def parse_par(self, i: int) -> int:
        i += 1
        while self.tokens[i].kind is not Tok.RPAR:
            i = self.parse_ops(i)
        return i + 1

    def parse_mkdate(self, i: int) -> int:
        i += 2  # mkdate (
        for _arg in range(3):
            while self.tokens[i].kind not in (Tok.COMMA, Tok.RPAR):
                i = self.parse_ops(i)
            i += 1  # , or )
        day, month, year = self.stack.pop(), self.stack.pop(), self.stack.pop()
        self.stack.append(MkDate(si_unit(Unit.UnixTimestamp), year, month, day))
        return i

    @staticmethod
    def datetime_parts(text: str) -> tuple[int, int, int, int, int, int]:
        digits = [int(d) for d in re.findall(r"\d+", text)]
        return tuple(digits + [0] * (6 - len(digits)))  # type: ignore[return-value]

    def handle_field(self, tok: Token) -> None:
        counter = Counter.named(tok.text)
        if counter is not None:
            self.stack.append(CounterField(si_unit(Unit.COUNTER), counter))
            return

        vname, named_unit = split_unit(tok.text)
        ref = self.resolve_field(vname, named_unit)
        self.stack.append(MeterField(si_unit(named_unit), vname, ref.slot, ref.display_unit))

    def handle_binary(self, tok: Token) -> None:
        right, left = self.stack.pop(), self.stack.pop()
        match tok.kind:
            case Op.ADD | Op.SUB:
                si = math_op_unit(tok.kind, left.si, right.si)
            case Op.MUL:
                si = left.si.mul(right.si)
            case Op.DIV:
                si = left.si.div(right.si)
            case _:
                si = si_unit(Unit.COUNTER)
        self.stack.append(Binary(si, tok.kind, left, right))

    def handle_unary(self, tok: Token) -> None:
        inner = self.stack.pop()
        si = inner.si.sqrt() if tok.kind is Op.SQRT else inner.si
        self.stack.append(Unary(si, tok.kind, inner))


STATE = MockObj("state")
DVE = literal("dve")  # null for a field with no matcher
COUNTER_TYPE = wmbus_ns.enum("DVEntryCounterType", is_class=True)


def _convert_emit(expr: Expression, src: SIUnit, dst: SIUnit) -> Expression:
    conv = conversion(src, dst)
    if conv is None:
        return double(math.nan)
    from_scale, to_scale = conv
    return expr if from_scale == to_scale else expr * double(from_scale) / double(to_scale)


def _convert_value(value: float, src: SIUnit, dst: SIUnit) -> float:
    conv = conversion(src, dst)
    if conv is None:
        return math.nan
    from_scale, to_scale = conv
    return value if from_scale == to_scale else (value * from_scale) / to_scale


def _math_op_emit(
    op: Op, l: Expression, left: SIUnit, r: Expression, right: SIUnit
) -> tuple[Expression, SIUnit]:
    """Only the unit combinations math_op_unit accepts get here."""
    ut = _unix_timestamp_exp()
    if left.exp == right.exp:
        return op.emit(_convert_emit(l, left, right), r), right
    if right.exp == ut:
        # Upstream swaps the operands, for subtraction too.
        return _math_op_emit(op, r, right, l, left)
    if _is_second(right.exp):
        converted = _convert_emit(r, right, si_unit(Unit.Second))
        return op.emit(l, converted), si_unit(Unit.UnixTimestamp)
    signed_r = -r if op is Op.SUB else r
    return wmbus_ns.add_months(l, cast("int", signed_r)), si_unit(Unit.UnixTimestamp)


def emit(node: Node, to: SIUnit) -> Expression:
    counter = si_unit(Unit.COUNTER)

    match node:
        case Constant(value=value):
            return double(_convert_value(value, node.si, to))

        case DateTimeLiteral(parts=parts):
            return _convert_emit(wmbus_ns.datetime_literal(*parts), node.si, to)

        case MeterField(slot=slot, display_unit=display_unit):
            return _convert_emit(STATE.field_value(slot), si_unit(display_unit), to)

        case CounterField(counter=counter_type):
            return _convert_emit(
                wmbus_ns.counter_value(DVE, getattr(COUNTER_TYPE, counter_type)), node.si, to
            )

        case MkDate(year=year, month=month, day=day):
            return wmbus_ns.make_date(
                emit(year, counter), emit(month, counter), emit(day, counter)
            )

        case Unary(op=Op.SQRT, inner=inner):
            return _convert_emit(Op.SQRT.emit(emit(inner, inner.si)), node.si, to)

        case Unary(op=op, inner=inner):
            # Upstream does not convert a rounded result to `to`.
            return op.emit(emit(inner, inner.si))

        case Binary(op=(Op.ADD | Op.SUB) as op, left=left, right=right):
            expr, si = _math_op_emit(
                op, emit(left, left.si), left.si, emit(right, right.si), right.si
            )
            return _convert_emit(expr, si, to)

        case Binary(op=(Op.MUL | Op.DIV) as op, left=left, right=right):
            return _convert_emit(op.emit(emit(left, left.si), emit(right, right.si)), node.si, to)

        case Binary(op=op, left=left, right=right):
            return op.emit(emit(left, counter), emit(right, counter))


def compile_formula(formula: str, resolve_field: ResolveField, display_unit: Unit) -> Expression:
    return emit(Parser(formula, resolve_field).parse(), si_unit(display_unit))


def referenced_fields(formula: str) -> set[tuple[str, Quantity]]:
    refs = set()
    for tok in tokenize(formula):
        if tok.kind is Tok.FIELD and Counter.named(tok.text) is None:
            vname, unit = split_unit(tok.text)
            if unit is not None:
                refs.add((vname, UNITS[unit].quantity))
    return refs


_TEMPLATE = re.compile(r"\{([^}]*)\}")


def _template_terms(formula: str) -> list[tuple[int, Counter | int]]:
    """`storage_counter-31counter` as signed terms: a counter, or a constant."""
    terms = []
    sign = 1
    tokens = re.findall(r"[+-]|\d+|[a-z_]+", formula)
    while tokens:
        token = tokens.pop(0)
        if token in "+-":
            sign = 1 if token == "+" else -1
            continue
        counter = Counter.named(token)
        if counter is None:
            counter = int(token)
            tokens.pop(0)
        terms.append((sign, counter))
        sign = 1
    return terms


def template_counters(name: str) -> set[Counter]:
    return {
        term for formula in _TEMPLATE.findall(name)
        for _sign, term in _template_terms(formula) if isinstance(term, Counter)
    }


def expand_template(name: str, counters: dict[Counter, int]) -> str:
    def replace(match: re.Match) -> str:
        value = sum(
            sign * (counters[term] if isinstance(term, Counter) else term)
            for sign, term in _template_terms(match.group(1))
        )
        return str(value)

    return _TEMPLATE.sub(replace, name)
