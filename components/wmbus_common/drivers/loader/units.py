"""StrEnums over tables/*.yaml, so a typo fails at import rather than in C++."""

import math
from dataclasses import dataclass
from enum import StrEnum
from functools import cache
from pathlib import Path

import yaml

TABLES_DIR = Path(__file__).parents[2] / "tables"

EXPONENT_KEYS = ("s", "m", "kg", "a", "mol", "cd", "k", "c", "f", "month", "year", "ut")


@cache
def table(name: str):
    return yaml.safe_load((TABLES_DIR / f"{name}.yaml").read_text(encoding="utf-8"))


@dataclass(frozen=True)
class SIExp:
    exps: tuple[int, ...] = (0,) * len(EXPONENT_KEYS)
    nonlinear: bool = False

    @staticmethod
    def from_table(si: dict) -> "SIExp":
        return SIExp(tuple(si.get(k, 0) for k in EXPONENT_KEYS), si.get("nonlinear", False))

    def mul(self, other: "SIExp") -> "SIExp":
        return SIExp(tuple(a + b for a, b in zip(self.exps, other.exps)))

    def div(self, other: "SIExp") -> "SIExp":
        return SIExp(tuple(a - b for a, b in zip(self.exps, other.exps)))

    def sqrt(self) -> "SIExp":
        # Truncating towards zero, as the C++ division does.
        return SIExp(tuple(int(a / 2) for a in self.exps))


def _str_enum(name: str, names) -> type[StrEnum]:
    # The functional StrEnum would lowercase the C++ enumerator names.
    return StrEnum(name, [(n, n) for n in names])


# "Any" is the matcher's neutral value, not one of upstream's names, and
# "Unknown" is the quantity a computed unit has.
VifRange = _str_enum("VifRange", ["Any"] + list(table("vif_ranges")))
VifCombinable = _str_enum("VifCombinable", ["Any"] + list(table("vif_combinables")))
MeasurementType = _str_enum("MeasurementType", ["Any", "Instantaneous", "Minimum", "Maximum", "AtError"])
Quantity = _str_enum("Quantity", ["Unknown"] + list(table("quantities")))
Unit = _str_enum("Unit", table("units"))
# "None" is one of the enumerators and not a name Python can spell.
VifScaling = _str_enum("VifScaling", ["Auto", "None"])
DifSignedness = _str_enum("DifSignedness", ["Signed", "Unsigned"])


@dataclass(frozen=True)
class UnitInfo:
    symbol: str  # the field name suffix: total_m3
    display: str
    quantity: Quantity
    si: tuple[float, SIExp] | None  # scale and exponents; None for text and dates


UNITS = {
    Unit(name): UnitInfo(
        u["symbol"],
        u["display"],
        Quantity(u["quantity"]),
        (u["si"]["scale"], SIExp.from_table(u["si"])) if "si" in u else None,
    )
    for name, u in table("units").items()
}
BY_SYMBOL = {info.symbol: unit for unit, info in UNITS.items()}
# Upstream reads a unit by its name or by its symbol: DateLT, but kwh.
Unit._missing_ = classmethod(lambda cls, value: BY_SYMBOL.get(value))
DEFAULT_UNIT = {Quantity(q): Unit(u) for q, u in table("quantities").items()}


def split_unit(name: str) -> tuple[str, Unit | None]:
    """total_m3 -> (total, M3); a name without a unit suffix stays whole."""
    vname, _, suffix = name.rpartition("_")
    if vname and suffix in BY_SYMBOL:
        return vname, BY_SYMBOL[suffix]
    return name, None


# Upstream's unit conversions count a year as 365.2425 days; only date arithmetic keeps years apart.
SECONDS_PER_YEAR = 3600.0 * 24.0 * 365.2425


def _in_seconds(si: tuple[float, SIExp]) -> tuple[float, tuple[int, ...]]:
    scale, exp = si
    exps = dict(zip(EXPONENT_KEYS, exp.exps))
    years = exps.pop("year")
    exps["s"] += years
    return scale * SECONDS_PER_YEAR**years, tuple(exps.values())


def unit_factor(src: Unit, dst: Unit) -> float:
    """NaN where the two do not differ by a factor: text, dates, dBm, temperature scales."""
    if src == dst:
        return 1.0
    a, b = UNITS[src].si, UNITS[dst].si
    if a is None or b is None or a[1].nonlinear or b[1].nonlinear:
        return math.nan
    (a_scale, a_exps), (b_scale, b_exps) = _in_seconds(a), _in_seconds(b)
    return a_scale / b_scale if a_exps == b_exps else math.nan
