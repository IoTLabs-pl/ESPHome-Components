from abc import ABC, abstractmethod
from dataclasses import dataclass, fields
from typing import ClassVar

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.cpp_generator import LambdaExpression

panasonic_aquarea_ns = cg.esphome_ns.namespace("panasonic_aquarea")

BinaryExtractor = panasonic_aquarea_ns.class_("BinaryExtractor")
FloatExtractor = panasonic_aquarea_ns.class_("FloatExtractor")
StringArrayExtractor = panasonic_aquarea_ns.class_("StringArrayExtractor")
StringMapExtractor = panasonic_aquarea_ns.class_("StringMapExtractor")
LambdaExtractor = panasonic_aquarea_ns.class_("LambdaExtractor")


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

    def build(self) -> cg.MockObj:
        return BinaryExtractor.new(self.byte, self.bit)


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

    def build(self) -> cg.MockObj:
        extractor_type = StringArrayExtractor.template(len(self.labels))
        initializer = cg.ArrayInitializer(*self.labels, multiline=True)
        return extractor_type.new(
            self.byte,
            self.bit,
            self.bit_width,
            initializer,
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

    def build(self) -> cg.MockObj:
        keys = tuple(key if isinstance(key, tuple) else (key,) for key in self.labels)
        key_length = len(keys[0])

        assert all(len(key) == key_length for key in keys), (
            "All keys in labels dict must have the same length"
        )

        extractor_type = StringMapExtractor.template(key_length)
        initializer = cg.ArrayInitializer(
            *zip(
                ([cv.HexInt(byte) for byte in key] for key in keys),
                self.labels.values(),
            ),
            multiline=True,
        )

        return extractor_type.new(
            self.byte,
            self.bit,
            self.bit_width,
            initializer,
        )


@dataclass(frozen=True)
class LambdaExtractorConfig(ExtractorConfig):
    decoder: str
    value_type: ClassVar[cg.MockObj]

    def build(self) -> cg.MockObj:
        return LambdaExtractor.template(self.value_type).new(
            LambdaExpression(
                (self.decoder,),
                parameters=[
                    (
                        cg.std_vector.template(cg.uint8)
                        .operator("ref")
                        .operator("const"),
                        "data",
                    ),
                ],
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
