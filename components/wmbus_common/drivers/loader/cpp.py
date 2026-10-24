"""C++ emitted for the compiler only; not formatted."""

import math
from dataclasses import dataclass
from functools import partial
from pathlib import Path
from typing import Literal

import jinja2
from esphome.cpp_generator import (
    ArrayInitializer,
    Expression,
    FloatLiteral,
    MockObj,
    RawExpression,
    StructInitializer,
    UnaryOpExpression,
    literal,
)
from esphome.cpp_types import global_ns

wmbus_ns = global_ns.namespace("wmbus")

# Designated members keep a row honest when a header reorders them.
Aggregate = partial(StructInitializer, RawExpression(""))

TEMPLATES = jinja2.Environment(
    loader=jinja2.FileSystemLoader(Path(__file__).parent / "templates"),
    undefined=jinja2.StrictUndefined,
    trim_blocks=True,
    lstrip_blocks=True,
    keep_trailing_newline=True,
)


@dataclass
class Declaration:
    kind: Literal["array", "value", "formula"]
    type: str
    symbol: str
    value: Expression


class Tables:
    """A driver's static declarations, in the order they refer to each other."""

    def __init__(self):
        self.declarations: list[Declaration] = []
        self.shared: dict[str, dict] = {}

    def span(self, type_: str, symbol: str, rows: list) -> Expression:
        if not rows:
            return ArrayInitializer()
        self.declarations.append(Declaration("array", type_, symbol, ArrayInitializer(*rows, multiline=True)))
        return ArrayInitializer(literal(symbol), len(rows))

    def shared_span(self, type_: str, prefix: str, key, rows: list) -> Expression:
        """One table per distinct key, named prefix_0, prefix_1..."""
        made = self.shared.setdefault(prefix, {})
        if key not in made:
            made[key] = self.span(type_, f"{prefix}_{len(made)}", rows)
        return made[key]

    def define(self, type_: str, symbol: str, value: Expression) -> Expression:
        self.declarations.append(Declaration("value", type_, symbol, value))
        return MockObj(UnaryOpExpression("&", literal(symbol)))

    def formula(self, symbol: str, value: Expression) -> Expression:
        self.declarations.append(Declaration("formula", "double", symbol, value))
        return literal(symbol)


class _DoubleLiteral(FloatLiteral):
    def __str__(self) -> str:
        if math.isinf(self.f):
            return "INFINITY" if self.f > 0 else "(-INFINITY)"
        text = super().__str__().removesuffix("f")
        return f"({text})" if text.startswith("-") else text


def double(value: float) -> MockObj:
    return MockObj(_DoubleLiteral(value))


def enumerator(enum: str, name: str) -> MockObj:
    """wmbus::enum::name"""
    return getattr(wmbus_ns.enum(enum, is_class=True), name)
