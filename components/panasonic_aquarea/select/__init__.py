from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import select

from .. import panasonic_aquarea_ns, ReadableEntity, WritableEntity
from ..extractor import (
    StringArrayExtractorConfig,
    StringMapExtractorConfig,
)
from ..platform_descriptor import PlatformDescriptor, Platform


SelectClass = panasonic_aquarea_ns.class_(
    "Select",
    select.Select,
    cg.Component,
    WritableEntity,
    ReadableEntity,
)


@dataclass(frozen=True)
class SelectDescriptor(PlatformDescriptor):
    """
    Describes a writable select entity for heatpump mode/option control.
    Extracts and encodes enumerated values using bit patterns mapped to
    human-readable options. Similar to text_sensor but supports write operations.
    Used for operation modes, zone selection, holiday mode, etc.
    """

    set: int
    icon: str = cv.UNDEFINED

    ALLOWED_EXTRACTORS: ClassVar = (
        StringArrayExtractorConfig,
        StringMapExtractorConfig,
    )

    def create_entity_schema(self) -> cv.Schema:
        return select.select_schema(
            SelectClass,
            icon=self.icon,
        )

    async def create_entity(self, config: dict):
        # Get options for new_select
        if isinstance(self.extractor, StringArrayExtractorConfig):
            options = list(self.extractor.labels)
        elif isinstance(self.extractor, StringMapExtractorConfig):
            options = list(self.extractor.labels.values())
        else:
            options = []

        return await select.new_select(config, options=options)


# Create platform instance
platform = Platform(Path(__file__).parent, SelectDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
