import re
from dataclasses import dataclass
from enum import StrEnum
from functools import cached_property


class FieldType(StrEnum):
    NUMERIC = "Numeric"
    STRING = "String"


@dataclass(frozen=True, order=True)
class FieldDefinition:
    field_type: FieldType
    name: str

    @cached_property
    def __matcher(self) -> re.Pattern:
        # The field name can contain a {number} placeholder, which matches any digits.
        # This allows to match fields like "current_power_consumption_1_kw",
        # "current_power_consumption_2_kw", etc. with a single definition
        # "current_power_consumption_{number}_kw".
        return re.compile(re.sub(r"\{[^}]+?}", r"\\d+", self.name))

    def match(self, field_name: str, type_hint: FieldType | None = None) -> bool:
        match = self.__matcher.fullmatch(field_name) is not None
        if type_hint and match and self.field_type != type_hint:
            raise ValueError(
                f"Field '{field_name}' matched definition '{self.name}' but has type {type_hint}, expected {self.field_type}"
            )
        return match
