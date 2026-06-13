"""Button platform using generic descriptor."""

from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import button

from .. import panasonic_aquarea_ns, WritableEntity
from ..extractor import BinaryExtractorConfig
from ..platform_descriptor import PlatformDescriptor, Platform


ButtonClass = panasonic_aquarea_ns.class_(
    "Button",
    button.Button,
    cg.Component,
    WritableEntity,
)


@dataclass(frozen=True)
class ButtonDescriptor(PlatformDescriptor):
    """
    Describes a writable button (momentary) entity.
    The corresponding bit is toggled when pressed.
    """

    set: int | str

    ALLOWED_EXTRACTORS: ClassVar = (BinaryExtractorConfig,)

    def create_entity_schema(self) -> cv.Schema:
        return button.button_schema(class_=ButtonClass)

    async def create_entity(self, config: dict):
        return await button.new_button(config)


# Create platform instance
platform = Platform(Path(__file__).parent, ButtonDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
