"""The XMQ subset upstream's drivers use; a repeated key becomes a list of dicts."""

import re
from dataclasses import dataclass, field
from typing import ClassVar


@dataclass(frozen=True)
class Token:
    text: str
    column: int = field(default=0, compare=False)
    pattern: ClassVar[re.Pattern]

    @classmethod
    def at(cls, text: str, position: int) -> "Token":
        column = position - (text.rfind("\n", 0, position) + 1)
        for kind in cls.__subclasses__():
            if match := kind.pattern.match(text, position):
                return kind(match.group(), column)


class Blank(Token):
    pattern = re.compile(r"[ \t\r\n]+|//[^\n]*\n?|/\*.*?\*/", re.DOTALL)


class Quoted(Token):
    """'' is the empty string; a run of N>=3 quotes wraps content ending in the same run."""

    pattern = re.compile(r"""('{3,}|"{3,}|'|")(.*?)\1""", re.DOTALL)

    @property
    def value(self) -> str:
        # Continuation lines lose their common indent, at most up to the quote's column.
        first, *rest = self.pattern.match(self.text).group(2).split("\n")
        indents = [len(line) - len(line.lstrip(" ")) for line in rest if line.strip()]
        strip = min(min(indents, default=0), self.column + 1)
        return "\n".join([first] + [line[strip:] for line in rest])


class Punct(Token):
    pattern = re.compile(r"[{}()=,]")


class Bare(Token):
    pattern = re.compile(r"[^ \t\r\n{}()=]+")

    @property
    def value(self) -> str:
        return self.text


def tokenize(text: str):
    position = 0
    while position < len(text):
        token = Token.at(text, position)
        position += len(token.text)
        if not isinstance(token, Blank):
            yield token


def _add(parent: dict, key: str, value) -> None:
    if key not in parent:
        parent[key] = value
    elif isinstance(parent[key], list):
        parent[key].append(value)
    else:
        parent[key] = [parent[key], value]


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

    def parse_block(self) -> dict:
        node: dict = {}
        while (token := self.take()) not in (None, Punct("}")):
            name = token.value
            attrs = self.parse_attributes() if self.peek() == Punct("(") else {}

            match self.peek():
                case Punct("{"):
                    self.take()
                    _add(node, name, {**attrs, **self.parse_block()})
                case Punct("="):
                    self.take()
                    value = self.take().value
                    _add(node, name, {**attrs, "_content": value} if attrs else value)
                case _:
                    _add(node, name, attrs or "")
        return node

    def parse_attributes(self) -> dict:
        self.take()  # (
        attrs = {}
        while (token := self.take()) != Punct(")"):
            if token == Punct(","):
                continue
            attrs[token.value] = ""
            if self.peek() == Punct("="):
                self.take()
                attrs[token.value] = self.take().value
        return attrs


def parse_xmq(content: str) -> dict:
    return _Parser(content).parse_block()
