import re
from dataclasses import dataclass
from enum import StrEnum
from functools import cached_property


class FieldType(StrEnum):
    NUMERIC = "Numeric"
    STRING = "String"


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
        return re.compile(re.sub(r"\{[^}]+?}", r"\\d+", self.name))

    def match(self, field_name: str, type_hint: FieldType | None = None) -> bool:
        matched = self._pattern.fullmatch(field_name) is not None
        if type_hint and matched and self.field_type != type_hint:
            raise ValueError(
                f"Field '{field_name}' matched '{self.name}' "
                f"but has type {type_hint}, expected {self.field_type}"
            )
        return matched

    def __eq__(self, other) -> bool:
        if isinstance(other, FieldDefinition):
            return self.name == other.name and self.field_type == other.field_type
        if isinstance(other, str):
            return self._pattern.fullmatch(other) is not None
        return NotImplemented

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
