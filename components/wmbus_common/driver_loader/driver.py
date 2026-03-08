import re
from dataclasses import dataclass, field
from difflib import get_close_matches
from functools import cached_property
from logging import getLogger
from pathlib import Path

from .field import FieldDefinition, FieldType, MatchResult
from .xmq_loader import generate as load_xmq

LOGGER = getLogger(__name__)

ADD_FIELD_METHOD_RE = re.compile(
    r'(?P<comment_mark>/[*/]\s*)?add(?P<field_type>Numeric|String)Field\w*?\([^"]*"(?P<name>[^"]+)"[^"]*"(?P<info>[^"]+)".*?\);',
    flags=re.MULTILINE | re.DOTALL,
)

ADD_LIBRARY_FIELD_METHOD_RE = re.compile(
    r'(?P<comment_mark>/[*/]\s*)?addOptionalLibraryFields\("(?P<names>[^"]+)"\);',
    flags=re.MULTILINE | re.DOTALL,
)

WELL_KNOWN_FIELDS = {
    "rssi_dbm",
    "timestamp",
}

RELOCATED_CPP_HEADERS = {
    "manufacturer_specificities",
    "meters_common_implementation",
    "wmbus_utils",
}


@dataclass(frozen=True)
class Driver:
    name: str
    source_path: Path = field(repr=False)
    cpp_source: str = field(repr=False)

    def __lt__(self, other):
        if isinstance(other, Driver):
            return self.name < other.name

        return NotImplemented

    @cached_property
    def fields(self) -> set[FieldDefinition]:
        return {
            FieldDefinition(
                field_type=FieldType(match["field_type"]),
                name=match["name"],
            )
            for match in ADD_FIELD_METHOD_RE.finditer(self.cpp_source)
            if match["comment_mark"] is None
        } | {
            FieldDefinition(
                field_type=FieldType.LIBRARY,
                name=name,
            )
            for match in ADD_LIBRARY_FIELD_METHOD_RE.finditer(self.cpp_source)
            if match["comment_mark"] is None
            for name in match["names"].split(",")
        }

    @property
    def available_fields(self):
        return sorted({f.name for f in self.fields} | WELL_KNOWN_FIELDS)

    def request_field(self, field_name: str, field_type: FieldType | None = None):
        if field_name in WELL_KNOWN_FIELDS:
            return field_name

        matches = [(f, f.match(field_name, field_type)) for f in self.fields]

        exact = [f for f, r in matches if r is MatchResult.MATCH]
        if exact:
            for f in exact:
                f.serialize = True
            return field_name

        partial = [f for f, r in matches if r is MatchResult.PARTIAL_MATCH]
        if partial:
            for f in partial:
                f.serialize = True
            LOGGER.warning(
                "Requested %s field '%s' from driver '%s' has no exact match, "
                "using %d partial match(es): %s",
                field_type.lower(),
                field_name,
                self.name,
                len(partial),
                ", ".join(f"{f.name} ({f.field_type.value})" for f in partial),
            )
            return field_name

        field_names = sorted({f.name for f in self.fields} | WELL_KNOWN_FIELDS)
        suggestions = get_close_matches(field_name, field_names)

        LOGGER.warning(
            "Unknown field '%s' requested from driver '%s', %s options are: %s",
            field_name,
            self.name,
            "similar" if suggestions else "valid",
            ", ".join(suggestions or field_names),
        )
        return f"{field_name} [unknown]"

    def serialize(self) -> str:
        requested_fields = {f for f in self.fields if f.serialize} or self.fields
        regular_fields_to_remove = {
            f.name
            for f in (self.fields - requested_fields)
            if f.field_type != FieldType.LIBRARY
        }
        library_fields_to_remove = {
            f.name
            for f in (self.fields - requested_fields)
            if f.field_type == FieldType.LIBRARY
        }

        source = self.cpp_source

        def regular_field_replacer(match: re.Match) -> str:
            if match["comment_mark"] is not None:
                return match[0]

            full_call = match[0]
            field_name = match["name"]

            if field_name in regular_fields_to_remove:
                return f"/* {full_call} */"
            else:
                help_start, help_end = match.span("info")

                call_start = match.start(0)
                rel_start = help_start - call_start - 1
                rel_end = help_end - call_start + 1

                return f'{full_call[:rel_start]}"" /* {full_call[rel_start:rel_end]} */{full_call[rel_end:]}'

        source = ADD_FIELD_METHOD_RE.sub(regular_field_replacer, self.cpp_source)

        def library_field_replacer(match: re.Match) -> str:
            if match["comment_mark"] is not None:
                return match[0]

            full_call = match[0]
            old_content = match["names"]
            new_content = ",".join(
                set(old_content.split(",")) - library_fields_to_remove
            )

            arg_start, arg_end = match.span("names")
            call_start = match.start(0)
            rel_start = arg_start - call_start - 1
            rel_end = arg_end - call_start + 1

            return f'{full_call[:rel_start]}"{new_content}" /* "{old_content}" */{full_call[rel_end:]}'

        source = ADD_LIBRARY_FIELD_METHOD_RE.sub(library_field_replacer, source)

        return source


@dataclass(frozen=True, order=True)
class CppDriver(Driver):
    @classmethod
    def from_source(cls, path: Path) -> "CppDriver":
        name = path.stem.removeprefix("driver_")
        cpp_source = path.read_text()

        for hdr in RELOCATED_CPP_HEADERS:
            cpp_source = cpp_source.replace(
                f"{hdr}.h",
                f"../esphome/components/wmbus_common/{hdr}.h",
            )
        return cls(name=name, source_path=path, cpp_source=cpp_source)


@dataclass(frozen=True, order=True)
class XmqDriver(Driver):
    @classmethod
    def from_source(cls, path: Path) -> "XmqDriver":
        name = path.stem
        xmq_source = path.read_text()
        cpp_source = load_xmq(xmq_source)
        return cls(name=name, source_path=path, cpp_source=cpp_source)
