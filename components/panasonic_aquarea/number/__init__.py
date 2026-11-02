from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import number
from esphome.cpp_generator import LambdaExpression, statement
from esphome.helpers import fnv1a_32bit_hash

from .. import ReadableEntity, WritableEntity, panasonic_aquarea_ns
from ..descriptor_defaults import DEFAULT_UNITS
from ..extractor import FloatExtractorConfig
from ..platform_descriptor import Platform, PlatformDescriptor, variant_condition

NumberClass = panasonic_aquarea_ns.class_(
    "Number",
    number.Number,
    cg.Component,
    WritableEntity,
    ReadableEntity,
)
VariantNumberClass = panasonic_aquarea_ns.class_(
    "VariantNumber",
    NumberClass,
)


CONF_VARIANTS_ID = "variants_id"


@dataclass(frozen=True, kw_only=True)
class NumberTraits:
    min: float
    max: float
    step: float = 1


@dataclass(frozen=True, kw_only=True)
class NumberVariant(NumberTraits):
    condition: dict[str, str]


@dataclass(frozen=True)
class NumberDescriptor(PlatformDescriptor, NumberTraits):
    """
    Describes a writable number entity for heatpump parameter control.
    Supports byte-based data extraction and encoding with linear value
    normalization: value = (raw + offset). Used for setpoints like
    temperature requests, pump duty, timer values, etc.
    """

    set: int | str
    device_class: str = cv.UNDEFINED
    entity_category: str = cv.UNDEFINED
    variants: tuple[NumberVariant, ...] = ()

    ALLOWED_EXTRACTORS: ClassVar = (FloatExtractorConfig,)

    @classmethod
    def from_yaml(
        cls, platform: "Platform", key: int | str, values: dict
    ) -> "NumberDescriptor":
        if "variants" in values:
            variants = tuple(NumberVariant(**v) for v in values["variants"])
            base = variants[0]
            values = {
                **values,
                "variants": variants,
                "min": base.min,
                "max": base.max,
                "step": base.step,
            }

        return super().from_yaml(platform, key, values)

    def create_entity_schema(self) -> cv.Schema:
        unit_of_measurement = DEFAULT_UNITS.get(self.device_class, cv.UNDEFINED)
        var_class = VariantNumberClass if self.variants else NumberClass
        schema = number.number_schema(
            class_=var_class,
            unit_of_measurement=unit_of_measurement,
            device_class=self.device_class,
            entity_category=self.entity_category,
        )
        if self.variants:
            schema = schema.extend(
                {
                    cv.GenerateID(CONF_VARIANTS_ID): cv.declare_id(
                        VariantNumberClass.struct("Variant")
                    ),
                }
            )
        return schema

    async def create_entity(self, config: dict):
        var = await number.new_number(
            config,
            min_value=self.min,
            max_value=self.max,
            step=self.step,
        )

        if self.variants:
            num = cg.MockObj("num", "->")
            variant_inits = []
            for v in self.variants:
                apply_fn = LambdaExpression(
                    [
                        statement(num.traits.set_min_value(v.min)),
                        statement(num.traits.set_max_value(v.max)),
                        statement(num.traits.set_step(v.step)),
                    ],
                    parameters=[(VariantNumberClass.operator("ptr"), cg.MockObj("num"))],
                    capture="",
                    return_type=cg.void,
                )
                variant_inits.append(
                    cg.StructInitializer(
                        VariantNumberClass.struct("Variant"),
                        ("condition", variant_condition(v.condition)),
                        ("apply", apply_fn),
                    )
                )

            arr = cg.static_const_array(
                config[CONF_VARIANTS_ID], cg.ArrayInitializer(*variant_inits)
            )
            cg.add(var.set_variants(fnv1a_32bit_hash(self.name), arr))

        return var


# Create platform instance
platform = Platform(Path(__file__).parent, NumberDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
