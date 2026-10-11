from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.components import number
from esphome.cpp_generator import LambdaExpression, statement

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
        values = dict(values)
        if "variants" in values:
            variants_raw = list(values.pop("variants"))
            common = {k: values.pop(k) for k in ("min", "max", "step") if k in values}
            common.setdefault("step", 1.0)

            if "min" not in common or "max" not in common:
                base = {**common, **variants_raw.pop(0)}
            elif variants_raw and "condition" not in variants_raw[0]:
                base = {**common, **variants_raw.pop(0)}
            else:
                base = common

            values["min"] = base["min"]
            values["max"] = base["max"]
            values["step"] = base.get("step", 1.0)

            values["variants"] = tuple(
                NumberVariant(
                    condition=v["condition"],
                    min=v.get("min", values["min"]),
                    max=v.get("max", values["max"]),
                    step=v.get("step", values["step"]),
                )
                for v in variants_raw
            )

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
            all_variants = (self, *self.variants)
            min_varies = len({v.min for v in all_variants}) > 1
            max_varies = len({v.max for v in all_variants}) > 1
            step_varies = len({v.step for v in all_variants}) > 1

            pref_key = cg.RawExpression(f'fnv1a_hash("{self.name}")')
            num = cg.MockObj("num", "->")
            variant_inits = []
            for v in self.variants:
                cond_fn = variant_condition(v.condition)
                apply_stmts = []
                if min_varies:
                    apply_stmts.append(statement(num.traits.set_min_value(v.min)))
                if max_varies:
                    apply_stmts.append(statement(num.traits.set_max_value(v.max)))
                if step_varies:
                    apply_stmts.append(statement(num.traits.set_step(v.step)))

                apply_fn = LambdaExpression(
                    apply_stmts,
                    parameters=[(VariantNumberClass.operator("ptr"), cg.MockObj("num"))],
                    capture="",
                    return_type=cg.void,
                )
                variant_inits.append(
                    cg.StructInitializer(
                        VariantNumberClass.struct("Variant"),
                        ("condition", cond_fn),
                        ("apply", apply_fn),
                    )
                )

            arr_id = config[CONF_VARIANTS_ID]
            arr = cg.static_const_array(arr_id, cg.ArrayInitializer(*variant_inits))
            cg.add(var.set_variants(pref_key, arr))

        return var


# Create platform instance
platform = Platform(Path(__file__).parent, NumberDescriptor)
CONFIG_SCHEMA = platform.CONFIG_SCHEMA
to_code = platform.to_code
