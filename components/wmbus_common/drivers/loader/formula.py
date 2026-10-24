"""Compiles XMQ `calculate` formulas to C++ at codegen, keeping upstream's quirks.

  * no precedence: left to right, a binary operator takes one operand on its right,
  * + and - convert the left side to the right side's unit,
  * counter operators (%, >>, ==, &, ...) and round/floor/ceil ignore the target unit,
  * a date literal (mktime in the device's zone) and adding months stay run-time calls.

Unsupported, as no driver uses them: **, time literals, temperature scale conversion,
adding years to a date. Upstream has parsed every formula already, so the syntax and
the field references are taken as valid.
"""

import math
import operator
import re
from dataclasses import dataclass
from enum import Enum, StrEnum
from typing import Callable

from esphome.cpp_generator import BinOpExpression, Expression, MockObj, RawExpression, literal
from esphome.cpp_types import std_ns

from .cpp import double, enumerator, wmbus_ns
from .units import BY_SYMBOL, UNITS, Quantity, SIExp, Unit, split_unit


@dataclass(frozen=True)
class SIUnit:
    scale: float
    exp: SIExp

    def mul(self, other: "SIUnit") -> "SIUnit":
        return SIUnit(self.scale * other.scale, self.exp.mul(other.exp))

    def div(self, other: "SIUnit") -> "SIUnit":
        return SIUnit(self.scale / other.scale, self.exp.div(other.exp))

    def sqrt(self) -> "SIUnit":
        return SIUnit(math.sqrt(self.scale), self.exp.sqrt())


def si_unit(unit: Unit) -> SIUnit:
    """A unit without SI terms (TXT, DateTimeLT) converts to nothing."""
    si = UNITS[unit].si
    return SIUnit(*si) if si else SIUnit(0.0, SIExp())


COUNTER = si_unit(Unit.COUNTER)
SECOND = si_unit(Unit.Second)
MONTH = si_unit(Unit.Month)
TIMESTAMP = si_unit(Unit.UnixTimestamp)
ZERO = double(0.0)
ONE = double(1.0)


def ternary(cond: Expression, if_true: Expression, if_false: Expression) -> MockObj:
    return MockObj(RawExpression(f"({cond} ? {if_true} : {if_false})"))


def _truth(spelling: str) -> tuple:
    # && and || are not operators Python can overload.
    return spelling, 2, lambda a, b: ternary(MockObj(BinOpExpression(a != ZERO, spelling, b != ZERO)), ONE, ZERO)


def _comparison(spelling: str, compare: Callable) -> tuple:
    return spelling, 2, lambda a, b: ternary(compare(a, b), ONE, ZERO)


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

    def __init__(self, spelling: str, arity: int, emit: Callable[..., Expression]):
        self.spelling = spelling
        self.arity = arity
        self.emit = emit


FIXED = {op.spelling: op for op in Op} | {text: text for text in ("(", ")", ",", "mkdate")}
# Longest spelling first, so that >> wins over >. Upstream wants a character after a spelled-out
# operator, so one cannot end a formula, and tries units before fields.
_FIXED = "|".join(re.escape(s) + ("(?=[^a-z])" if s.isalpha() else "") for s in sorted(FIXED, key=len, reverse=True))
_UNITS = "|".join(map(re.escape, sorted(BY_SYMBOL, key=len, reverse=True)))
TOKEN = re.compile(
    r"\s+"
    r"|(?P<datetime>'\d{4}-\d{2}-\d{2}( \d{2}:\d{2}:\d{2})?')"
    r"|(?P<number>[0-9]+\.?[0-9]*)(?![0-9.])"
    f"|(?P<fixed>{_FIXED})"
    f"|(?P<unit>{_UNITS})(?![a-z0-9_])"
    r"|(?P<field>[a-z][a-z0-9_]*)"
)


def tokenize(formula: str) -> list[tuple[object, str]]:
    """(kind, text): an Op, punctuation, mkdate, or the name of the group that matched."""
    tokens, pos = [], 0
    while pos < len(formula):
        m = TOKEN.match(formula, pos)
        pos = m.end()
        if m.lastgroup:
            tokens.append((FIXED[m["fixed"]] if m.lastgroup == "fixed" else m.lastgroup, m[m.lastgroup]))
    return tokens


class Counter(StrEnum):
    """DVEntryCounterType enumerators; formulas name them in lower case."""

    STORAGE_COUNTER = "STORAGE_COUNTER"
    TARIFF_COUNTER = "TARIFF_COUNTER"
    SUBUNIT_COUNTER = "SUBUNIT_COUNTER"

    @property
    def prefix(self) -> str:
        """As in storage_nr."""
        return self.lower().removesuffix("_counter")


COUNTERS = {c.lower(): c for c in Counter}


def convert(expr: Expression, src: SIUnit, dst: SIUnit) -> Expression:
    """expr * from / to; NaN between units that differ in more than scale."""
    if src.exp.exps != dst.exp.exps:
        return double(math.nan)
    return expr if src.scale == dst.scale else expr * double(src.scale) / double(dst.scale)


def convert_value(value: float, src: SIUnit, dst: SIUnit) -> float:
    if src.exp.exps != dst.exp.exps:
        return math.nan
    return value if src.scale == dst.scale else value * src.scale / dst.scale


def math_op_unit(op: Op, left: SIUnit, right: SIUnit) -> SIUnit | None:
    if left.exp == right.exp:
        return None if op is Op.ADD and left.exp == TIMESTAMP.exp else right
    if right.exp == TIMESTAMP.exp:
        return math_op_unit(op, right, left)
    if left.exp == TIMESTAMP.exp and right.exp in (SECOND.exp, MONTH.exp):
        return TIMESTAMP
    return None


def math_op_emit(op: Op, l: Expression, left: SIUnit, r: Expression, right: SIUnit) -> tuple[Expression, SIUnit]:
    """Only the unit combinations math_op_unit accepts get here."""
    if left.exp == right.exp:
        return op.emit(convert(l, left, right), r), right
    if right.exp == TIMESTAMP.exp:
        # Upstream swaps the operands, for subtraction too.
        return math_op_emit(op, r, right, l, left)
    if right.exp == SECOND.exp:
        return op.emit(l, convert(r, right, SECOND)), TIMESTAMP
    months = literal("static_cast").template(literal("int"))(-r if op is Op.SUB else r)
    return wmbus_ns.add_months(l, months), TIMESTAMP


STATE = MockObj("state")
DVE = literal("dve")  # null for a field with no matcher


@dataclass
class Value:
    """A parsed operand: its unit, and its expression in the unit asked for."""

    si: SIUnit | None
    emit: Callable[[SIUnit], Expression]


# (vname, the unit the formula names it in) -> (slot, display unit) of the field it refers to.
ResolveField = Callable[[str, Unit], tuple[int, Unit]]


def compile_formula(formula: str, resolve_field: ResolveField, display_unit: Unit) -> Expression:
    tokens = tokenize(formula)
    stack: list[Value] = []

    def operand(i: int) -> int:
        """Pushes the operand at token i and returns the index after it; a comma, a closing parenthesis
        or a bare unit is for the caller to read."""
        kind, text = tokens[i] if i < len(tokens) else (None, None)
        match kind:
            case "field" if text in COUNTERS:
                counter = enumerator("DVEntryCounterType", COUNTERS[text])
                stack.append(Value(COUNTER, lambda to: convert(wmbus_ns.counter_value(DVE, counter), COUNTER, to)))

            case "field":
                vname, unit = split_unit(text)
                slot, stored = resolve_field(vname, unit)
                stack.append(Value(si_unit(unit), lambda to: convert(STATE.field_value(slot), si_unit(stored), to)))

            case "datetime":
                parts = [int(d) for d in re.findall(r"\d+", text)]
                parts += [0] * (6 - len(parts))
                stack.append(Value(TIMESTAMP, lambda to: convert(wmbus_ns.datetime_literal(*parts), TIMESTAMP, to)))

            case "number":
                si, value = si_unit(BY_SYMBOL[tokens[i + 1][1]]), float(text)
                stack.append(Value(si, lambda to: double(convert_value(value, si, to))))
                return i + 2

            case Op(arity=2) as op:
                after = operand(i + 1)
                right, left = stack.pop(), stack.pop()
                stack.append(binary(op, left, right))
                return after

            case Op(arity=1) as op:
                after = operand(i + 1)
                inner = stack.pop()
                if op is Op.SQRT:
                    si = inner.si.sqrt()
                    stack.append(Value(si, lambda to: convert(op.emit(inner.emit(inner.si)), si, to)))
                else:
                    # Upstream does not convert a rounded result to `to`.
                    stack.append(Value(inner.si, lambda to: op.emit(inner.emit(inner.si))))
                return after

            case "mkdate":
                i += 2  # mkdate (
                for _arg in range(3):
                    while tokens[i][0] not in (",", ")"):
                        i = operand(i)
                    i += 1
                day, month, year = stack.pop(), stack.pop(), stack.pop()
                parts = (year, month, day)
                stack.append(Value(TIMESTAMP, lambda to: wmbus_ns.make_date(*(p.emit(COUNTER) for p in parts))))
                return i

            case "(":
                i += 1
                while tokens[i][0] != ")":
                    i = operand(i)

            case _:
                return i
        return i + 1

    i = 0
    while (after := operand(i)) != i:
        i = after
    return stack.pop().emit(si_unit(display_unit))


def binary(op: Op, left: Value, right: Value) -> Value:
    match op:
        case Op.ADD | Op.SUB:
            return Value(
                math_op_unit(op, left.si, right.si),
                lambda to: convert(*math_op_emit(op, left.emit(left.si), left.si, right.emit(right.si), right.si), to),
            )
        case Op.MUL | Op.DIV:
            si = left.si.mul(right.si) if op is Op.MUL else left.si.div(right.si)
            return Value(si, lambda to: convert(op.emit(left.emit(left.si), right.emit(right.si)), si, to))
    return Value(COUNTER, lambda to: op.emit(left.emit(COUNTER), right.emit(COUNTER)))


def referenced_fields(formula: str) -> set[tuple[str, Quantity]]:
    refs = set()
    for kind, text in tokenize(formula):
        if kind == "field" and text not in COUNTERS:
            vname, unit = split_unit(text)
            if unit is not None:
                refs.add((vname, UNITS[unit].quantity))
    return refs


TEMPLATE = re.compile(r"\{([^}]*)\}")


def _template_terms(expr: str) -> list[tuple[int, Counter | int]]:
    """`storage_counter-31counter` as signed terms: a counter, or a constant."""
    tokens = iter(re.findall(r"[+-]|\d+|[a-z_]+", expr))
    terms, sign = [], 1
    for token in tokens:
        if token in "+-":
            sign = 1 if token == "+" else -1
            continue
        term = COUNTERS.get(token)
        if term is None:
            term = int(token)
            next(tokens)  # its unit, counter
        terms.append((sign, term))
        sign = 1
    return terms


def template_counters(name: str) -> set[Counter]:
    terms = (term for expr in TEMPLATE.findall(name) for _sign, term in _template_terms(expr))
    return {term for term in terms if isinstance(term, Counter)}


def expand_template(name: str, counters: dict[Counter, int]) -> str:
    return TEMPLATE.sub(
        lambda m: str(sum(sign * (counters[t] if isinstance(t, Counter) else t) for sign, t in _template_terms(m[1]))),
        name,
    )
