from abc import ABC, abstractmethod
from dataclasses import dataclass, fields
from typing import ClassVar

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.cpp_generator import LambdaExpression

from . import panasonic_aquarea_ns

BitField = panasonic_aquarea_ns.class_("BitField")
BinaryExtractor = panasonic_aquarea_ns.class_("BinaryExtractor")
FloatExtractor = panasonic_aquarea_ns.class_("FloatExtractor")
ArrayIndexExtractor = panasonic_aquarea_ns.class_("ArrayIndexExtractor")
MapIndexExtractor = panasonic_aquarea_ns.class_("MapIndexExtractor")
LambdaExtractor = panasonic_aquarea_ns.class_("LambdaExtractor")

FRAME_PARAMETER = (cg.std_span.template(cg.uint8.operator("const")), cg.MockObj("data"))


class ExtractorConfig(ABC):
    """Builds a concrete extractor from YAML values."""

    @classmethod
    def from_yaml(cls, values: dict) -> tuple["ExtractorConfig", dict]:
        field_names = {f.name for f in fields(cls) if f.init}
        used = {k: v for k, v in values.items() if k in field_names}
        remaining = {k: v for k, v in values.items() if k not in field_names}
        return cls(**used), remaining

    @abstractmethod
    def build(self) -> cg.MockObj:
        raise NotImplementedError


@dataclass(frozen=True)
class BinaryExtractorConfig(ExtractorConfig):
    byte: int
    bit: int
    bit_width: int = 2

    def build(self) -> cg.MockObj:
        return BinaryExtractor.new(self.byte, self.bit, self.bit_width)


@dataclass(frozen=True)
class FloatExtractorConfig(ExtractorConfig):
    byte: int
    bit_width: int = 8
    offset: float = 0.0
    multiplier: float = 1.0

    def build(self) -> cg.MockObj:
        return FloatExtractor.new(
            self.byte,
            self.bit_width,
            self.offset,
            self.multiplier,
        )


@dataclass(frozen=True)
class StringArrayExtractorConfig(ExtractorConfig):
    byte: int
    bit: int
    bit_width: int
    labels: list[str]

    def __post_init__(self):
        if not isinstance(self.labels, list):
            raise ValueError("labels must be a list of strings")

    @property
    def options(self) -> list[str]:
        return self.labels

    def build(self) -> cg.MockObj:
        return ArrayIndexExtractor.new(
            self.byte, self.bit, self.bit_width, len(self.labels)
        )

    def condition(self, value: str) -> LambdaExpression:
        raw = self.labels.index(value) + 1
        field = BitField(self.byte, self.bit, self.bit_width)
        _, data = FRAME_PARAMETER
        return LambdaExpression(
            ("return ", field.read_bits(data) == raw, ";"),
            parameters=[FRAME_PARAMETER],
            capture="",
            return_type=cg.bool_,
        )


@dataclass(frozen=True)
class StringMapExtractorConfig(ExtractorConfig):
    byte: int
    bit: int
    bit_width: int
    labels: dict[int | tuple[int, ...], str]

    def __post_init__(self):
        if not isinstance(self.labels, dict):
            raise ValueError("labels must be a dict mapping int or tuple to string")

    @property
    def options(self) -> list[str]:
        return list(self.labels.values())

    def build(self) -> cg.MockObj:
        keys = tuple(key if isinstance(key, tuple) else (key,) for key in self.labels)
        key_length = len(keys[0])

        assert all(len(key) == key_length for key in keys), (
            "All keys in labels dict must have the same length"
        )

        table = cg.shared_progmem_array(
            "panasonic_aquarea_keys",
            cg.std_ns.class_("array").template(cg.uint8, key_length),
            cg.ArrayInitializer(
                *(cg.ArrayInitializer(*map(cv.HexInt, key)) for key in keys),
                multiline=True,
            ),
        )
        return MapIndexExtractor.template(key_length).new(
            self.byte, self.bit, self.bit_width, table
        )


@dataclass(frozen=True)
class LambdaExtractorConfig(ExtractorConfig):
    decoder: str
    value_type: ClassVar[cg.MockObj]

    def build(self) -> cg.MockObj:
        return LambdaExtractor.template(self.value_type).new(
            LambdaExpression(
                (self.decoder,),
                parameters=[FRAME_PARAMETER],
                capture="",
                return_type=cg.optional.template(self.value_type),
            )
        )

    @classmethod
    def create(cls, value_type_: cg.MockObj) -> type["LambdaExtractorConfig"]:
        """Create a LambdaExtractorConfig subclass with a predefined return type."""

        @dataclass(frozen=True)
        class _TypedLambdaExtractorConfig(cls):
            value_type: ClassVar[cg.MockObj] = value_type_

        return _TypedLambdaExtractorConfig
