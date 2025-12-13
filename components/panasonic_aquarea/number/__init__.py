from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import number

from .. import panasonic_aquarea_ns, WritableEntity, ReadableEntity
from ..descriptor_defaults import DEFAULT_UNITS
from ..extractor import FloatExtractorConfig
from ..platform_descriptor import PlatformDescriptor, Platform


NumberClass = panasonic_aquarea_ns.class_(
    "Number",
    number.Number,
    cg.Component,
    WritableEntity,
    ReadableEntity,
)


@dataclass(frozen=True)
class NumberDescriptor(PlatformDescriptor):
    """
    Describes a writable number entity for heatpump parameter control.
    Supports byte-based data extraction and encoding with linear value
    normalization: value = (raw + offset). Used for setpoints like
    temperature requests, pump duty, timer values, etc.
    """

    set: int | str
    min: float
    max: float
    device_class: str = cv.UNDEFINED
    entity_category: str = cv.UNDEFINED

    ALLOWED_EXTRACTORS: ClassVar = (FloatExtractorConfig,)

    def create_entity_schema(self) -> cv.Schema:
        unit_of_measurement = DEFAULT_UNITS.get(self.device_class, cv.UNDEFINED)
        return number.number_schema(
            class_=NumberClass,
            unit_of_measurement=unit_of_measurement,
            device_class=self.device_class,
            entity_category=self.entity_category,
        )

    async def create_entity(self, config: dict):
        return await number.new_number(
            config,
            min_value=self.min,
            max_value=self.max,
            step=1,
        )


# Create platform instance
platform = Platform(Path(__file__).parent, NumberDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
