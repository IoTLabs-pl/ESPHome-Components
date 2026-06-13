from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import switch

from .. import panasonic_aquarea_ns, WritableEntity, ReadableEntity
from ..extractor import BinaryExtractorConfig
from ..platform_descriptor import PlatformDescriptor, Platform


SwitchClass = panasonic_aquarea_ns.class_(
    "Switch",
    switch.Switch,
    cg.Component,
    WritableEntity,
    ReadableEntity,
)


@dataclass(frozen=True)
class SwitchDescriptor(PlatformDescriptor):
    """
    Describes a switch entity for writable control.
    Each switch is based on two specific bits in a command byte.
    We mark the byte and bit positions that control the specific switch state.
    """

    set: int

    ALLOWED_EXTRACTORS: ClassVar = (BinaryExtractorConfig,)

    def create_entity_schema(self) -> cv.Schema:
        return switch.switch_schema(class_=SwitchClass)

    async def create_entity(self, config: dict):
        return await switch.new_switch(config)


# Create platform instance
platform = Platform(Path(__file__).parent, SwitchDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
