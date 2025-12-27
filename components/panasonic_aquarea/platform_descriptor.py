from abc import ABC, abstractmethod
from dataclasses import dataclass, fields
from pathlib import Path
from typing import Any, ClassVar, TypeVar, Generic

import yaml

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.core import ID
from esphome.const import CONF_NAME, CONF_ID

from . import Device, ReadableEntity, WritableEntity
from .extractor import ExtractorConfig

T = TypeVar("T")


@dataclass(frozen=True)
class PlatformDescriptor(ABC, Generic[T]):
    name: str
    extractor: ExtractorConfig

    ALLOWED_EXTRACTORS: ClassVar[tuple[type[ExtractorConfig], ...]] = ()

    @classmethod
    def _id_field_name(cls) -> str:
        allowed_ids = {"set", "top"}
        field_names = {f.name for f in fields(cls)}
        if common := allowed_ids & field_names:
            return common.pop()
        raise ValueError(f"No suitable id field found for {cls.__name__}")

    @property
    def id_value(self) -> int | str:
        return getattr(self, self._id_field_name())

    @abstractmethod
    def create_entity_schema(self) -> cv.Schema:
        raise NotImplementedError

    @abstractmethod
    async def create_entity(self, config: dict) -> cg.MockObj:
        raise NotImplementedError

    @classmethod
    def from_yaml(cls, key: int | str, values: dict) -> "PlatformDescriptor":
        values = dict(values)
        data = {}

        # Special case for sensor extras: key starts with 'x'
        if isinstance(key, str) and key.startswith("x"):
            data["extra"] = True

        extractor, values = cls._pick_extractor(values)
        data["extractor"] = extractor
        data[cls._id_field_name()] = key

        return cls(**data, **values)

    @classmethod
    def _pick_extractor(cls, values: dict) -> tuple[ExtractorConfig, dict]:
        if not cls.ALLOWED_EXTRACTORS:
            raise ValueError(f"No extractors configured for {cls.__name__}")

        errors: list[Exception] = []
        for extractor_cls in cls.ALLOWED_EXTRACTORS:
            try:
                return extractor_cls.from_yaml(values)
            except Exception as exc:  # noqa: BLE001 - aggregate errors for clarity
                errors.append(exc)

        raise ExceptionGroup(f"Could not match extractor for {cls.__name__}", errors)

    @property
    def config_schema(self) -> cv.Schema:
        return self.create_entity_schema().extend(
            {
                cv.Required(self._id_field_name()): self.id_value,
                cv.GenerateID("parent_id"): cv.use_id(Device),
                cv.Optional(CONF_NAME, default=self.name): cv._validate_entity_name,
            }
        )

    async def to_code(self, config: dict) -> Any:
        # Create entity
        id: ID = config[CONF_ID]
        var = await self.create_entity(config)

        # Create and set extractor
        cg.add(var.set_extractor(self.extractor.build()))

        # Register with parent
        parent = await cg.get_variable(config["parent_id"])

        # Register on hub component if needed (readable entities)
        if id.type.inherits_from(ReadableEntity):
            cg.add(parent.add_entity(var, hasattr(self, "extra") and self.extra))

        # Register as parented if needed (writable entities)
        if id.type.inherits_from(WritableEntity):
            await cg.register_parented(var, parent)

        await cg.register_component(var, config)

        return var

    @classmethod
    def load(cls, module_path: Path) -> dict[int | str, "PlatformDescriptor"]:
        yaml_path = module_path / "descriptors.yaml"

        config: dict = yaml.full_load(yaml_path.read_text())

        return {key: cls.from_yaml(key, values) for key, values in config.items()}


class Platform:
    def __init__(
        self,
        module_path: Path,
        descriptor_class: type[PlatformDescriptor],
    ):
        yaml_path = module_path / "descriptors.yaml"
        config: dict = yaml.full_load(yaml_path.read_text())

        self._descriptors: dict[int | str, PlatformDescriptor] = {}
        self._descriptor_class = descriptor_class

        for key, values in config.items():
            errors = []
            for cls in self._descriptor_class.__subclasses__() or {
                self._descriptor_class
            }:
                try:
                    descriptor = cls.from_yaml(key, values)
                    self._descriptors[key] = descriptor
                    break
                except Exception as e:
                    errors.append(e)
            else:
                raise ExceptionGroup(
                    f"Could not find suitable descriptor class for key '{key}'", errors
                )

        assert self._descriptors, "No descriptors loaded!"

    @property
    def _id_field(self) -> str:
        return self._descriptor_class._id_field_name()

    @property
    def CONFIG_SCHEMA(self) -> cv.Schema:
        def schema_validator(config):
            """Validator that looks up the descriptor and applies its schema."""
            descriptor_id = config[self._id_field]
            return self._descriptors[descriptor_id].config_schema(config)

        return cv.All(
            cv.Schema(
                {cv.Required(self._id_field): cv.one_of(*self._descriptors)},
                extra=cv.ALLOW_EXTRA,
            ),
            schema_validator,
        )

    async def to_code(self, config: dict) -> Any:
        return await self._descriptors[config[self._id_field]].to_code(config)
