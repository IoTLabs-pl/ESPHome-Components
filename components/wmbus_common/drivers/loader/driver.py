from dataclasses import dataclass, field
from functools import cached_property
from pathlib import Path
from typing import Self

import esphome.config_validation as cv
from esphome.core import CORE

from .cpp import TEMPLATES
from .xmq import parse_xmq
from .xmq_loader import SCHEMA, Expanded, FieldKind, concrete_fields, driver_symbol, generate_from_definition

XMQ_DRIVERS_PATH = Path(__file__).parents[1] / "src"

GENERATED_DIR = "wmbusmeters_generated"

DOMAIN = "wmbus_common"

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
    name: str
    fields: set[FieldDefinition]

    @property
    def requested(self) -> set[str] | None:
        """Fields this config asked for, None if it never asked; CORE.reset() clears them for the next config."""
        return CORE.data.get(DOMAIN, {}).get(self.name)

    def request(self) -> Self:
        CORE.data.setdefault(DOMAIN, {}).setdefault(self.name, set())
        return self

    def available_fields(self, kind: FieldKind | None = None) -> list[str]:
        return sorted(
            {f.name for f in self.fields if kind is None or f.kind is kind}
            | {n for n, kinds in WELL_KNOWN_FIELDS.items() if kind is None or kind in kinds}
        )

    def kinds_of(self, field_name: str) -> set[FieldKind]:
        return WELL_KNOWN_FIELDS.get(field_name, set()) | {f.kind for f in self.fields if f.name == field_name}

    def request_field(self, field_name: str, kind: FieldKind | None = None) -> str:
        kinds = self.kinds_of(field_name)
        if kinds and kind is not None and kind not in kinds:
            raise cv.Invalid(
                f"field '{field_name}' of driver '{self.name}' holds "
                f"{' and '.join(sorted(kinds))} values, not {kind}"
            )
        cv.one_of(*self.available_fields(kind))(field_name)
        self.request().requested.add(field_name)
        return field_name


@dataclass(frozen=True, order=True)
class Driver(FieldSelection):
    name: str
    source_path: Path = field(compare=False, repr=False)

    @classmethod
    def from_source(cls, path: Path) -> "Driver":
        return cls(name=path.stem, source_path=path)

    @cached_property
    def _definition(self) -> dict:
        return SCHEMA(parse_xmq(self.source_path.read_text()))["driver"]

    @cached_property
    def concrete_fields(self) -> list[Expanded]:
        return concrete_fields(self._definition)

    @cached_property
    def fields(self) -> set[FieldDefinition]:
        return {FieldDefinition(f.definition.kind, f.json_name) for f in self.concrete_fields}

    @property
    def aliases(self) -> list[str]:
        return self._definition["aliases"]

    @property
    def symbol(self) -> str:
        return driver_symbol(self.name)

    @property
    def meter_type(self) -> str | None:
        return self._definition.get("meter_type")

    @property
    def manufacturer(self) -> str | None:
        return self._definition.get("manufacturer")

    @property
    def model(self) -> str | None:
        return self._definition.get("model")

    @property
    def default_fields(self) -> list[str]:
        return self._definition["default_fields"]

    def serialize(self, every_field: bool = False) -> str:
        # Requested without any field means every field, too.
        fields = None if every_field else self.requested or None
        return generate_from_definition(self._definition, self.source_path.name, fields)


@dataclass(frozen=True)
class AutoDriver(FieldSelection):
    """`type: auto`: fields are those of every compiled driver; generates no source."""

    manager: "DriverManager" = field(repr=False, compare=False)
    name: str = "auto"

    @property
    def fields(self) -> set[FieldDefinition]:
        return {f for d in self.manager.registered_drivers for f in d.fields}


class DriverManager:
    def __init__(self):
        self._all_drivers: dict[str, Driver] = {}
        self._aliases: dict[str, str] = {}
        self.auto_driver = AutoDriver(manager=self)
        for p in sorted(XMQ_DRIVERS_PATH.glob("*.xmq")):
            driver = Driver.from_source(p)
            self._all_drivers[driver.name] = driver
            for alias in driver.aliases:
                self._aliases.setdefault(alias, driver.name)

    @property
    def available_drivers(self) -> list[str]:
        return sorted(set(self._all_drivers) | set(self._aliases))

    def request_driver(self, driver_name: str) -> Driver:
        return self._all_drivers[self._aliases.get(driver_name, driver_name)].request()

    @property
    def auto_requested(self) -> bool:
        return self.auto_driver.requested is not None

    @property
    def registered_drivers(self) -> list[Driver]:
        return [d for d in self._all_drivers.values() if d.requested is not None]

    def registry_index(self, driver: Driver) -> int:
        return self.registered_drivers.index(driver)

    def sync_to_directory(self, target_dir: str | Path) -> None:
        """Outside src/esphome/, because writer.copy_src_tree() deletes anything there it did not put."""
        files = {
            "registry.h": TEMPLATES.get_template("registry.h.j2").render(
                symbols=[d.symbol for d in self.registered_drivers]
            ),
            # An auto meter may bind to any compiled driver, so none can be trimmed.
            **{f"{d.name}.cpp": d.serialize(every_field=self.auto_requested) for d in self.registered_drivers},
        }

        target_dir = Path(target_dir)
        target_dir.mkdir(exist_ok=True, parents=True)

        for name, content in files.items():
            path = target_dir / name
            if not path.exists() or path.read_text() != content:
                path.write_text(content)

        # esp-idf globs sources with CONFIGURE_DEPENDS, so stale files would compile.
        for path in target_dir.iterdir():
            if path.name not in files:
                path.unlink()
