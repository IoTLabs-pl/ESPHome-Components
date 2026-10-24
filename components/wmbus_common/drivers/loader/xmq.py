"""The XMQ subset upstream's drivers use; a repeated key becomes a list of dicts."""

import re

TOKEN = re.compile(
    r"""[ \t\r\n]+|//[^\n]*\n?|/\*.*?\*/"""
    r"""|(?P<quote>'{3,}|"{3,}|'|")(?P<quoted>.*?)(?P=quote)"""
    r"""|(?P<punct>[{}()=,])"""
    r"""|(?P<bare>[^ \t\r\n{}()=]+)""",
    re.DOTALL,
)


def dedent(text: str, column: int) -> str:
    """Continuation lines lose their common indent, at most up to the quote's column."""
    first, *rest = text.split("\n")
    indents = [len(line) - len(line.lstrip(" ")) for line in rest if line.strip()]
    strip = min(min(indents, default=0), column + 1)
    return "\n".join([first] + [line[strip:] for line in rest])


def tokenize(text: str) -> list[tuple[str | None, str | None]]:
    """(punctuation, None) or (None, value); '' is the empty string, N>=3 quotes wrap text ending in the same run."""
    tokens = []
    for m in TOKEN.finditer(text):
        if m["punct"]:
            tokens.append((m["punct"], None))
        elif m["bare"]:
            tokens.append((None, m["bare"]))
        elif m["quote"]:
            tokens.append((None, dedent(m["quoted"], m.start() - text.rfind("\n", 0, m.start()) - 1)))
    return tokens


def _add(parent: dict, key: str, value) -> None:
    if key not in parent:
        parent[key] = value
    elif isinstance(parent[key], list):
        parent[key].append(value)
    else:
        parent[key] = [parent[key], value]


def parse_xmq(content: str) -> dict:
    tokens = tokenize(content)[::-1]

    def peek() -> str | None:
        return tokens[-1][0] if tokens else None

    def value() -> str:
        punct, text = tokens.pop() if tokens else ("the end", None)
        if text is None:
            raise ValueError(f"XMQ: expected a name or a value, got {punct!r}")
        return text

    def attributes() -> dict:
        tokens.pop()  # (
        attrs = {}
        while peek() != ")":
            if peek() == ",":
                tokens.pop()
                continue
            name = value()
            attrs[name] = ""
            if peek() == "=":
                tokens.pop()
                attrs[name] = value()
        tokens.pop()
        return attrs

    def block() -> dict:
        node: dict = {}
        while tokens and peek() != "}":
            name = value()
            attrs = attributes() if peek() == "(" else {}
            match peek():
                case "{":
                    tokens.pop()
                    _add(node, name, attrs | block())
                case "=":
                    tokens.pop()
                    text = value()
                    _add(node, name, attrs | {"_content": text} if attrs else text)
                case _:
                    _add(node, name, attrs or "")
        if tokens:
            tokens.pop()  # }
        return node

    return block()
