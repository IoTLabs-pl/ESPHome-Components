from dataclasses import dataclass
from math import log10, ceil
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import sensor

from .. import panasonic_aquarea_ns, ReadableEntity
from ..descriptor_defaults import DEFAULT_UNITS, DEFAULT_STATE_CLASS
from ..extractor import (
    FloatExtractorConfig,
    LambdaExtractorConfig,
)
from ..platform_descriptor import PlatformDescriptor, Platform


SensorClass = panasonic_aquarea_ns.class_(
    "Sensor",
    sensor.Sensor,
    cg.Component,
    ReadableEntity,
)


@dataclass(frozen=True, kw_only=True)
class SensorDescriptor(PlatformDescriptor):
    """Descriptor for read-only numeric sensors."""

    top: int | str
    unit_of_measurement: str = cv.UNDEFINED
    device_class: str = cv.UNDEFINED
    icon: str = cv.UNDEFINED
    state_class: str = cv.UNDEFINED
    accuracy_decimals: int = cv.UNDEFINED
    extra: bool = False

    ALLOWED_EXTRACTORS: ClassVar = (
        LambdaExtractorConfig.create(cg.float_),
        FloatExtractorConfig,
    )

    def create_entity_schema(self) -> cv.Schema:
        if self.accuracy_decimals is cv.UNDEFINED:
            multiplier = (
                self.extractor.multiplier
                if isinstance(self.extractor, FloatExtractorConfig)
                else 1.0
            )
            decimals = 0 if multiplier >= 1 else ceil(log10(1 / multiplier))

        else:
            decimals = self.accuracy_decimals

        if self.unit_of_measurement is cv.UNDEFINED:
            unit_of_measurement = DEFAULT_UNITS.get(self.device_class, cv.UNDEFINED)
        else:
            unit_of_measurement = self.unit_of_measurement

        if self.state_class is cv.UNDEFINED:
            state_class = DEFAULT_STATE_CLASS.get(self.device_class, cv.UNDEFINED)
        else:
            state_class = self.state_class

        return sensor.sensor_schema(
            class_=SensorClass,
            unit_of_measurement=unit_of_measurement,
            accuracy_decimals=decimals,
            device_class=self.device_class,
            state_class=state_class,
            icon=self.icon,
        )

    async def create_entity(self, config: dict):
        return await sensor.new_sensor(config)


platform = Platform(Path(__file__).parent, SensorDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
