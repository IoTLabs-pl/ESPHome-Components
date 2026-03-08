import re
from dataclasses import dataclass
from enum import StrEnum, Enum, auto
from functools import cached_property

from .units import split_name_unit


class FieldType(StrEnum):
    NUMERIC = "Numeric"
    STRING = "String"
    LIBRARY = "Library"


class MatchResult(Enum):
    MATCH = auto()
    NO_MATCH = auto()
    PARTIAL_MATCH = auto()


FORMULA_RE = re.compile(r"\{[^}]+?}")


@dataclass
class FieldDefinition:
    field_type: FieldType
    name: str
    serialize: bool = False

    @cached_property
    def _pattern(self) -> re.Pattern:
        # The field name can contain a {number} placeholder, which matches any digits.
        # This allows to match fields like "current_power_consumption_1_kw",
        # "current_power_consumption_2_kw", etc. with a single definition
        # "current_power_consumption_{number}_kw".
        return re.compile(FORMULA_RE.sub(r"\\d+", self.name))

    def match(self, field_name: str, type_hint: FieldType | None = None) -> MatchResult:
        # alter formula expressions to number
        field_name = FORMULA_RE.sub('0', field_name)

        # and try to match with or without unit suffix
        variants = [field_name]
        if self.field_type == FieldType.NUMERIC:
            name_without_unit, unit = split_name_unit(field_name)
            if unit:
                variants.append(name_without_unit)

        matched = any(
            self._pattern.fullmatch(variant) is not None for variant in variants
        )

        if (
            type_hint
            and matched
            and self.field_type != FieldType.LIBRARY
            and self.field_type != type_hint
        ):
            return MatchResult.PARTIAL_MATCH

        if matched:
            return MatchResult.MATCH

        return MatchResult.NO_MATCH

    def __hash__(self) -> int:
        return hash((self.name, self.field_type))

    def __lt__(self, other) -> bool:
        if isinstance(other, FieldDefinition):
            return self.name < other.name
        if isinstance(other, str):
            return self.name < other
        return NotImplemented

    def __gt__(self, other) -> bool:
        return not (self < other or self == other)

    def __str__(self) -> str:
        return self.name
