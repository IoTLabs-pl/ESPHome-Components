from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import binary_sensor

from .. import panasonic_aquarea_ns, ReadableEntity
from ..extractor import BinaryExtractorConfig
from ..platform_descriptor import PlatformDescriptor, Platform


BinarySensorClass = panasonic_aquarea_ns.class_(
    "BinarySensor",
    binary_sensor.BinarySensor,
    cg.Component,
    ReadableEntity,
)


@dataclass(frozen=True)
class BinarySensorDescriptor(PlatformDescriptor):
    """
    Describes a binary sensor entity (read-only status).
    Each binary sensor extracts state from two specific bits
    in a response byte. We mark the byte and bit positions
    that are relevant for the specific sensor.
    """

    top: int
    device_class: str = cv.UNDEFINED

    ALLOWED_EXTRACTORS: ClassVar = (BinaryExtractorConfig,)

    def create_entity_schema(self) -> cv.Schema:
        return binary_sensor.binary_sensor_schema(
            class_=BinarySensorClass,
            device_class=self.device_class,
        )

    async def create_entity(self, config: dict):
        return await binary_sensor.new_binary_sensor(config)


# Create platform instance
platform = Platform(Path(__file__).parent, BinarySensorDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
