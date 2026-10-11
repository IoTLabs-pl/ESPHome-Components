from dataclasses import dataclass, field
from functools import cached_property
from pathlib import Path

import esphome.config_validation as cv

from .xmq import parse_xmq
from .xmq_loader import SCHEMA, FieldKind, concrete_fields, driver_symbol, generate_from_definition

# Answered by wmbus_meter from the telegram, not by the driver.
WELL_KNOWN_FIELDS = {
    "media": {FieldKind.Text},
    "rssi_dbm": {FieldKind.Numeric},
    "timestamp": {FieldKind.Numeric, FieldKind.Text},
}


@dataclass(frozen=True)
class FieldDefinition:
    kind: FieldKind
    name: str


class FieldSelection:
    fields: set[FieldDefinition]

    def available_fields(self, kind: FieldKind | None = None) -> list[str]:
        return sorted(
            {f.name for f in self.fields if kind is None or f.kind is kind}
            | {n for n, kinds in WELL_KNOWN_FIELDS.items() if kind is None or kind in kinds}
        )

    def kinds_of(self, field_name: str) -> set[FieldKind]:
        return WELL_KNOWN_FIELDS.get(field_name, set()) | {
            f.kind for f in self.fields if f.name == field_name
        }

    def request_field(self, field_name: str, kind: FieldKind | None = None) -> str:
        kinds = self.kinds_of(field_name)
        if kinds and kind is not None and kind not in kinds:
            raise cv.Invalid(
                f"field '{field_name}' of driver '{self.name}' holds "
                f"{' and '.join(sorted(kinds))} values, not {kind}"
            )
        cv.one_of(*self.available_fields(kind))(field_name)
        self.select(field_name)
        return field_name

    def select(self, field_name: str) -> None:
        pass


@dataclass(frozen=True, order=True)
class Driver(FieldSelection):
    name: str
    source_path: Path = field(compare=False, repr=False)
    # Empty means every field.
    requested: set[str] = field(default_factory=set, compare=False, repr=False)

    @classmethod
    def from_source(cls, path: Path) -> "Driver":
        return cls(name=path.stem, source_path=path)

    @cached_property
    def _definition(self) -> dict:
        return SCHEMA(parse_xmq(self.source_path.read_text()))["driver"]

    @cached_property
    def fields(self) -> set[FieldDefinition]:
        return {
            FieldDefinition(f.definition.kind, f.json_name)
            for f in concrete_fields(self._definition)
        }

    def select(self, field_name: str) -> None:
        self.requested.add(field_name)

    @property
    def aliases(self) -> list[str]:
        return self._definition["aliases"]

    @property
    def symbol(self) -> str:
        return driver_symbol(self.name)

    def serialize(self) -> str:
        return generate_from_definition(
            self._definition,
            self.source_path.name,
            include_fields=self.requested or None,
        )


@dataclass(frozen=True)
class AutoDriver(FieldSelection):
    """`type: auto`: fields are those of every compiled driver; generates no source."""

    manager: object = field(repr=False, compare=False)
    name: str = "auto"

    @property
    def fields(self) -> set[FieldDefinition]:
        return {f for d in self.manager.registered_drivers for f in d.fields}
