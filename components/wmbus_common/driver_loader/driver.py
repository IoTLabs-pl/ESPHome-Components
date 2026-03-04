import re
from dataclasses import dataclass, field
from difflib import get_close_matches
from functools import cached_property
from logging import getLogger
from pathlib import Path

from .field import FieldDefinition, FieldType
from .xmq_loader import generate as load_xmq

LOGGER = getLogger(__name__)

ADD_FIELD_METHOD_RE = re.compile(
    #  method name                 ("name"*otherargs*"info"*otherargs*);
    r'add(Numeric|String)Field\w*?\([^"]*"([^"]+)"[^"]*"([^"]+)".*?\);',
    flags=re.MULTILINE | re.DOTALL,
)

WELL_KNOWN_FIELDS = {
    "rssi",
    "timestamp",
}

RELOCATED_CPP_HEADERS = {
    "meters_common_implementation",
    "wmbus_utils"
}


@dataclass(frozen=True, order=True)
class Driver:
    name: str
    source_path: Path = field(compare=False, repr=False)
    cpp_source: str = field(compare=False, repr=False)

    @cached_property
    def fields(self) -> set[FieldDefinition]:
        return {
            FieldDefinition(
                field_type=FieldType(match[1]),
                name=match[2],
            )
            for match in ADD_FIELD_METHOD_RE.finditer(self.cpp_source)
        }

    def request_field(
        self, field_name: str, field_type: FieldType | None = None
    ) -> set[FieldDefinition]:
        if field_name in WELL_KNOWN_FIELDS:
            return set()

        matching_fields = [f for f in self.fields if f.match(field_name, field_type)]
        if not matching_fields:
            field_names = [f.name for f in self.fields] + list(WELL_KNOWN_FIELDS)
            matches = get_close_matches(field_name, field_names)

            LOGGER.warning(
                "Unknown field '%s' requested from driver '%s', %s options are: %s",
                field_name,
                self.name,
                "similar" if matches else "valid",
                ", ".join(matches or field_names),
            )

        return set(matching_fields)

    def serialize(self, requested_fields: set[FieldDefinition] | None = None) -> str:
        """Serialize driver C++ source, commenting out unrequested fields."""
        active_fields = requested_fields or self.fields
        fields_to_comment_out = {f.name for f in (self.fields - active_fields)}

        def replacer(match: re.Match) -> str:
            full_call = match.group(0)
            field_name = match.group(2)

            if field_name in fields_to_comment_out:
                return f"/* {full_call} */"
            else:
                help_start, help_end = match.span(3)

                call_start = match.start(0)
                rel_start = help_start - call_start - 1
                rel_end = help_end - call_start + 1

                return f'{full_call[:rel_start]}"" /* {full_call[rel_start:rel_end]} */{full_call[rel_end:]}'

        return ADD_FIELD_METHOD_RE.sub(replacer, self.cpp_source)


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
