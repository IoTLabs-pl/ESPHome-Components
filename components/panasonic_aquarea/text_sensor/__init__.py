from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import text_sensor

from .. import ReadableEntity, panasonic_aquarea_ns
from ..extractor import (
    StringArrayExtractorConfig,
    StringMapExtractorConfig,
    LambdaExtractorConfig,
)
from ..platform_descriptor import Platform, PlatformDescriptor

TextSensorClass = panasonic_aquarea_ns.class_(
    "TextSensor",
    text_sensor.TextSensor,
    cg.Component,
    ReadableEntity,
)


@dataclass(frozen=True)
class TextSensorDescriptor(PlatformDescriptor):
    top: int
    icon: str = cv.UNDEFINED

    ALLOWED_EXTRACTORS: ClassVar = (
        LambdaExtractorConfig.create(cg.std_string),
        StringArrayExtractorConfig,
        StringMapExtractorConfig,
    )

    def create_entity_schema(self) -> cv.Schema:
        return text_sensor.text_sensor_schema(
            TextSensorClass,
            icon=self.icon,
        )

    async def create_entity(self, config: dict):
        return await text_sensor.new_text_sensor(config)


# Create platform instance
platform = Platform(Path(__file__).parent, TextSensorDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
