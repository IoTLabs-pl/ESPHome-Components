"""Transpiles an XMQ driver to the C++ tables meter.cpp reads.

A key outside the schemas, or a construct the runtime does not implement, raises;
what upstream's own loader checks (references, masks, units) is taken as valid.
"""

import itertools
import math
import operator
import re
from dataclasses import dataclass, field, replace
from enum import StrEnum
from functools import cache, reduce
from pathlib import Path

from esphome.config_validation import boolean, ensure_list, hex_int
from esphome.core import HexInt
from esphome.cpp_generator import ArrayInitializer, Expression, StructInitializer
from esphome.cpp_types import nullptr, std_ns
from voluptuous import All, Any, Coerce, In, Optional, Remove, Required, Schema

from . import formula as F
from .cpp import TEMPLATES, Aggregate, Tables, double, enumerator, wmbus_ns
from .ixml import compile_to_tables
from .units import DEFAULT_UNIT, UNITS, Quantity, Unit, VifCombinable, VifRange, str_enum, table, unit_factor
from .xmq import parse_xmq

# XMQ's own vocabularies; every value is also the name of the C++ enumerator.
FieldKind = str_enum("FieldKind", ["Numeric", "Text"])
MEASUREMENT_TYPES = ("Any", "Instantaneous", "Minimum", "Maximum", "AtError")
MAP_TYPES = ("BitToString", "IndexToString", "DecimalsToString")
# DEPRECATED and HIDE are read and ignored; HIDE only shapes upstream's JSON.
ATTRIBUTES = ("REQUIRED", "DEPRECATED", "STATUS", "INCLUDE_TPL_STATUS", "INJECT_INTO_STATUS", "HIDE")
# A driver's transform_payload, and the C++ enumerator.
PAYLOAD_QUIRKS = {
    "diehl_prios": "DiehlPrios",
    "buggy_sanxing_609B": "Sanxing609B",
    "try_qundis_decode": "TryQundisDecode",
}


class TestBit(StrEnum):
    Set = "Set"
    NotSet = "NotSet"

    @classmethod
    def _missing_(cls, value):
        # Upstream reads it with strcasecmp, so both spellings are valid.
        return next((bit for bit in cls if bit.casefold() == str(value).strip().casefold()), None)


ANY_RANGE_MEMBERS = {
    VifRange.AnyVolumeVIF: [VifRange.Volume],
    VifRange.AnyEnergyVIF: [VifRange.EnergyWh, VifRange.EnergyMJ, VifRange.EnergyMWh, VifRange.EnergyGJ],
    VifRange.AnyPowerVIF: [VifRange.PowerW, VifRange.PowerJh],
}
DATE_RANGES = {VifRange.Date, VifRange.DateTime}
# The ranges upstream reads as text rather than as hex.
READABLE_RANGES = {
    VifRange.EnhancedIdentification, VifRange.FabricationNo, VifRange.HardwareVersion, VifRange.FirmwareVersion,
    VifRange.Medium, VifRange.Manufacturer, VifRange.ModelVersion, VifRange.SoftwareVersion, VifRange.Customer,
    VifRange.Location, VifRange.SpecialSupplierInformation, VifRange.ParameterSet,
}
JSON_DATES = {Unit.DateLT: "Date", Unit.DateTimeLT: "DateTime", Unit.DateTimeUTC: "TimestampUTC"}

CSV_LIST = All(str, lambda x: [s.strip() for s in x.split(",") if s.strip()])


def first(value):
    """Upstream reads the first of a repeated key."""
    return value[0] if isinstance(value, list) else value


def base16(value) -> HexInt:
    """Upstream reads these with strtol(s, NULL, 16): "10" is 0x10."""
    return HexInt(value if isinstance(value, int) else int(value, 16))


def frac(s: str) -> float:
    num, _, denom = s.partition("/")
    return float(num) / float(denom)


def manufacturer_code(flag: str) -> int:
    a, b, c = (ord(ch) - 64 for ch in flag)
    return a * 1024 + b * 32 + c


def parse_mvt(v: list[str]) -> tuple[HexInt, HexInt, HexInt]:
    """mvt = APA,05,06; * is 0xff, "any"."""
    flag, version, type_ = v
    mfct = manufacturer_code(flag) if len(flag) == 3 and flag.isalpha() and flag.isupper() else int(flag, 16)
    return HexInt(mfct), *(HexInt(0xFF if part.strip() == "*" else int(part, 16)) for part in (version, type_))


MATCHER_SCHEMA = {
    Optional("measurement_type"): In(MEASUREMENT_TYPES),
    Optional("difvifkey"): str,
    Optional("vif_range"): Coerce(VifRange),
    # Read only at field level upstream; a few drivers carry one here anyway.
    Remove("vif_scaling"): str,
    Optional("tariff_nr"): All(CSV_LIST, [Coerce(int)]),
    Optional("subunit_nr"): All(CSV_LIST, [Coerce(int)]),
    Optional("storage_nr"): All(CSV_LIST, [Coerce(int)]),
    Optional("index_nr"): Coerce(int),
    Optional("add_combinable"): ensure_list(Coerce(VifCombinable)),
    Optional("add_combinable_raw"): ensure_list(hex_int),
}
MAP_SCHEMA = Schema(
    {
        Required("name"): All(first, str),
        Optional("value"): base16,
        Optional("bit"): Coerce(int),
        Optional("info"): str,
        Required("test"): Coerce(TestBit),
    }
)
LOOKUP_SCHEMA = Schema(
    {
        Required("name"): All(first, str),
        Required("map_type"): In(MAP_TYPES),
        Required("mask_bits"): base16,
        Optional("default_message", default=""): str,
        Optional("map", default=[]): ensure_list(MAP_SCHEMA),
        Optional("pre_shift_right"): Coerce(int),
    }
)
FIELD_SCHEMA = Schema(
    {
        Required("name"): str,
        Required("quantity"): Coerce(Quantity),
        Optional("info"): str,
        Optional("attributes", default=""): All(CSV_LIST, [In(ATTRIBUTES)]),
        Optional("display_unit"): Coerce(Unit),
        Optional("override_vif_unit"): Coerce(Unit),
        Optional("match"): MATCHER_SCHEMA,
        Optional("calculate"): str,
        Optional("vif_scaling"): In(("Auto", "None")),
        Optional("dif_signedness"): In(("Signed", "Unsigned")),
        Optional("force_scale"): Any(Coerce(float), frac),
        Optional("null_value"): Coerce(float),
        Optional("lookup"): LOOKUP_SCHEMA,
        Optional("readable_string"): In(("Normal", "Reversed")),
        Optional("transform_payload"): str,
        Optional("ixml"): str,
        Optional("match_entire_payload"): boolean,
        Optional("match_entire_frame"): boolean,
        # Upstream ignores these beside a field; `change` only feeds Home Assistant discovery.
        Remove("default_message"): str,
        Remove("null_string"): str,
        Remove("change"): str,
        Remove("deprecated_by"): str,
    }
)
# Upstream fixes the rule's name and type; XMQ supplies the mask and the maps.
MFCT_TPL_STATUS_SCHEMA = Schema(
    {
        Optional("mask_bits", default=0xFF): base16,
        Optional("default_message", default="OK"): str,
        Optional("map", default=[]): ensure_list(MAP_SCHEMA),
    }
)

LIBRARY_FILE = Path(__file__).resolve().parents[1] / "library.xmq"


def as_list(value) -> list:
    return value if isinstance(value, list) else ([] if value is None else [value])


@cache
def library_fields() -> dict[str, dict]:
    """By template id and alias, for `library { use = ... }`."""
    fields = {}
    for template in as_list(parse_xmq(LIBRARY_FILE.read_text(encoding="utf-8"))["library"]["template"]):
        definition = {"quantity": "Text"} | {k: v for k, v in template.items() if k not in ("id", "aliases")}
        for name in [template["id"], *template.get("aliases", "").split(",")]:
            if name.strip():
                fields[name.strip()] = definition
    return fields


def map_key(entry: dict) -> int:
    """Upstream merges inherited map entries by this key."""
    return 1 << int(entry["bit"]) if "bit" in entry else base16(entry["value"])


def inherit_lookup(f: dict, template: dict) -> dict:
    """The field's own lookup keys and map entries win."""
    own, lent = f.get("lookup", {}), template.get("lookup", {})
    own_map = as_list(own.get("map"))
    mapped = {map_key(m) for m in own_map}
    lookup = {k: v for k, v in lent.items() if k != "map"} | own
    lookup["map"] = own_map + [m for m in as_list(lent.get("map")) if map_key(m) not in mapped]
    return {k: v for k, v in f.items() if k != "template"} | {"lookup": lookup}


def expand_library_and_templates(data: dict) -> dict:
    driver = data["driver"]
    templates = {t["name"]: t for t in as_list((driver.get("templates") or {}).get("template_field"))}

    fields = []
    for use in as_list((driver.get("library") or {}).get("use")):
        # `use = a,b` names two library fields, `use = 'a|help'` one whatever the help says.
        names, bar, _help = str(use).partition("|")
        for name in [names] if bar else [n for n in names.split(",") if n.strip()]:
            fields.append(library_fields()[name.strip()])
    for f in as_list((driver.get("fields") or {}).get("field")):
        fields.append(inherit_lookup(f, templates[f["template"]]) if "template" in f else f)

    rest = {k: v for k, v in driver.items() if k not in ("library", "templates", "fields")}
    return data | {"driver": rest | ({"fields": {"field": fields}} if fields else {})}


SCHEMA = All(
    expand_library_and_templates,
    Schema(
        {
            Required("driver"): {
                Required("name"): str,
                Remove("meter_type"): str,
                Remove("meter_type_text"): str,
                Remove("deprecated_by"): str,
                Optional("info"): str,
                Remove("manufacturer"): str,
                Remove("model"): str,
                Optional("aliases", default=""): CSV_LIST,
                Optional("link_mode"): str,
                Optional("force_media_type"): str,
                Optional("transform_payload"): In(PAYLOAD_QUIRKS),
                Optional("mfct_tpl_status_bits"): MFCT_TPL_STATUS_SCHEMA,
                Optional("compact_frame_formats"): Schema({Required("difvif"): ensure_list(str)}),
                Required("default_fields"): str,
                Required("detect"): {Required("mvt"): ensure_list(CSV_LIST, parse_mvt)},
                Optional("fields"): {Required("field"): ensure_list(FIELD_SCHEMA)},
                Remove("flags"): object,
                Remove("tests"): object,
                Remove("test"): object,
            }
        }
    ),
)


Range = tuple[int, int]


@dataclass
class Matcher:
    """Counter ranges are inclusive; None means any."""

    active: bool = False
    dif_vif_key: str = ""
    measurement_type: str = "Any"
    vif_range: VifRange = VifRange.Any
    combinables: list[Range] = field(default_factory=list)
    any_combinable: bool = False
    storage: Range | None = (0, 0)
    tariff: Range | None = (0, 0)
    subunit: Range | None = (0, 0)
    index_nr: int = 1


def counter_nr(values: list[int] | None, any_allowed: bool = True) -> Range | None:
    if not values:
        return 0, 0
    if len(values) > 1:
        return values[0], values[1]
    # AnyStorageNr/AnyTariffNr; upstream has no such case for subunit.
    return None if values[0] == -1 and any_allowed else (values[0], values[0])


def combinable_span(combinable: VifCombinable) -> Range:
    span = table("vif_combinables")[combinable]
    return span["from"], span["to"]


def build_matcher(match: dict | None) -> Matcher:
    if match is None:
        return Matcher()
    if key := match.get("difvifkey"):
        # set(DifVifKey) ends the build: the remaining keys are ignored.
        return Matcher(active=True, dif_vif_key=key)
    named = match.get("add_combinable", [])
    return Matcher(
        active=True,
        measurement_type=match.get("measurement_type", "Any"),
        vif_range=match.get("vif_range", VifRange.Any),
        combinables=[combinable_span(c) for c in named if c != VifCombinable.Any]
        + [(int(c), int(c)) for c in match.get("add_combinable_raw", [])],
        any_combinable=VifCombinable.Any in named,
        storage=counter_nr(match.get("storage_nr")),
        tariff=counter_nr(match.get("tariff_nr")),
        subunit=counter_nr(match.get("subunit_nr"), any_allowed=False),
        index_nr=match.get("index_nr") or 1,
    )


@dataclass
class Lookup:
    name: str
    map_type: str
    mask: HexInt
    default_message: str
    entries: list[tuple[HexInt, str, TestBit]]  # (from, to, test)
    pre_shift_right: int = 0


def build_lookup(name: str, map_type: str, lookup: dict) -> Lookup:
    """A mask of 0 means the map's bits."""
    entries = [(HexInt(1 << e["bit"]) if "bit" in e else e["value"], e["name"], e["test"]) for e in lookup["map"]]
    mask = lookup["mask_bits"] or reduce(operator.or_, (value for value, *_ in entries), 0)
    return Lookup(name, map_type, HexInt(mask), lookup["default_message"], entries, lookup.get("pre_shift_right", 0))


@dataclass
class FieldDef:
    name: str  # vname; may carry a {template}
    kind: FieldKind
    quantity: Quantity
    display_unit: Unit
    attributes: list[str]
    matcher: Matcher
    vif_scaling: str = "None"
    dif_signedness: str = "Signed"
    scale: float = 1.0
    source_unit: Unit | None = None  # override_vif_unit
    null_value: float | None = None
    calculate: str | None = None
    lookup: Lookup | None = None
    text_form: str = "Hex"
    # An ixml carrier runs over every record and keeps the last text.
    every_match: bool = False


def field_def(d: dict) -> FieldDef:
    f = FieldDef(
        d["name"], FieldKind.Numeric, d["quantity"], d.get("display_unit") or DEFAULT_UNIT[d["quantity"]],
        d["attributes"], build_matcher(d.get("match")),
    )
    if f.quantity in (Quantity.Text, Quantity.Flags):
        f.kind, f.display_unit = FieldKind.Text, Unit.TXT
        if lookup := d.get("lookup"):
            f.lookup = build_lookup(lookup["name"], lookup["map_type"], lookup)
        f.text_form = text_form(f.matcher.vif_range, d.get("readable_string"))
        f.every_match = "ixml" in d
    elif d.get("calculate"):
        f.calculate, f.vif_scaling = d["calculate"], "Auto"
    elif "match" in d:
        # override_vif_unit: the record holds this unit whatever its VIF says.
        f.source_unit = d.get("override_vif_unit")
        f.vif_scaling = "None" if f.source_unit else d.get("vif_scaling", "Auto")
        f.dif_signedness = d.get("dif_signedness", "Signed")
        f.scale = float(d.get("force_scale", 1.0))
        f.null_value = d.get("null_value")
    return f


def text_form(vif_range: VifRange, readable_string: str | None) -> str:
    if vif_range in DATE_RANGES:
        return vif_range
    if readable_string == "Reversed":
        return "Reversed"
    if readable_string == "Normal" or vif_range in READABLE_RANGES:
        return "Readable"
    return "Hex"


@dataclass
class Expanded:
    definition: FieldDef
    name: str  # vname
    matcher: Matcher
    extract_all: bool
    slot: int = -1

    @property
    def json_name(self) -> str:
        if self.definition.kind is not FieldKind.Numeric:
            return self.name
        return f"{self.name}_{UNITS[self.definition.display_unit].symbol}"


def declared_fields(driver: dict) -> list[dict]:
    return driver.get("fields", {}).get("field", [])


def concrete_fields(driver: dict) -> list[Expanded]:
    """Upstream's registration order; a {counter} in a name makes a field per value of the matcher's range."""
    fields = []
    for raw in declared_fields(driver):
        if "ixml" in raw and (raw.get("match_entire_payload") or raw.get("match_entire_frame")):
            continue  # it only derives the records the fields match
        d = field_def(raw)
        m = d.matcher
        extract_all = d.every_match or any(r is not None and r[0] != r[1] for r in (m.storage, m.tariff, m.subunit))
        counters = sorted(F.template_counters(d.name))
        if not counters:
            fields.append(Expanded(d, d.name, m, extract_all))
            continue
        ranges = [range(low, high + 1) for low, high in (getattr(m, c.prefix) for c in counters)]
        for values in itertools.product(*ranges):
            pinned = replace(m, **{c.prefix: (v, v) for c, v in zip(counters, values)})
            fields.append(Expanded(d, F.expand_template(d.name, dict(zip(counters, values))), pinned, extract_all))
    return fields


def select_fields(fields: list[Expanded], include: set[str] | None) -> list[Expanded]:
    """Adds what the selection needs: formula inputs and, for STATUS, the INJECT_INTO_STATUS fields."""
    if include is None:
        return fields

    def needs(d: FieldDef) -> set[int]:
        refs = F.referenced_fields(d.calculate) if d.calculate else set()
        return {
            j for j, e in enumerate(fields)
            if (e.name, e.definition.quantity) in refs
            or ("STATUS" in d.attributes and "INJECT_INTO_STATUS" in e.definition.attributes)
        }

    selected = {i for i, f in enumerate(fields) if f.json_name in include}
    pending = list(selected)
    while pending:
        for j in needs(fields[pending.pop()].definition) - selected:
            selected.add(j)
            pending.append(j)
    return [f for i, f in enumerate(fields) if i in selected]


def range_unit(vif_range: VifRange) -> Unit:
    return Unit(table("vif_ranges")[vif_range]["unit"])


def vif_spans(vif_range: VifRange) -> list[Range]:
    if vif_range is VifRange.Any:
        return []
    ranges = table("vif_ranges")
    return [(ranges[r]["from"], ranges[r]["to"]) for r in ANY_RANGE_MEMBERS.get(vif_range, [vif_range])]


def span_row(span: Range) -> Expression:
    return Aggregate(("from", HexInt(span[0])), ("to", HexInt(span[1])))


def counter_range(r: Range | None) -> Expression:
    """std::optional is no aggregate, so the range names its type."""
    if r is None:
        return std_ns.nullopt
    return StructInitializer(wmbus_ns.struct("CounterRange"), ("from", r[0]), ("to", r[1]))


def matcher_spec(tables: Tables, m: Matcher) -> Expression:
    def span_table(prefix: str, spans: list[Range]) -> Expression:
        return tables.shared_span("VifSpan", prefix, tuple(spans), [span_row(s) for s in spans])

    return Aggregate(
        ("dif_vif_key", m.dif_vif_key),
        ("combinables", span_table("combinables", m.combinables)),
        ("any_combinable", m.any_combinable),
        ("measurement_type", enumerator("MeasurementType", m.measurement_type)),
        ("vif_ranges", span_table("vif_ranges", vif_spans(m.vif_range))),
        ("storage", counter_range(m.storage)),
        ("tariff", counter_range(m.tariff)),
        ("subunit", counter_range(m.subunit)),
        ("index_nr", m.index_nr),
        ("active", m.active),
    )


def lookup_spec(tables: Tables, symbol: str, lookups: list[Lookup]) -> Expression:
    rules = [
        Aggregate(
            ("name", lk.name),
            ("type", enumerator("MapType", lk.map_type)),
            ("mask", lk.mask),
            ("default_message", lk.default_message),
            ("map", tables.span("LookupMap", f"{symbol}_map_{i}", [
                Aggregate(("from", frm), ("to", to), ("test", enumerator("TestBit", test)))
                for frm, to, test in lk.entries
            ])),
            ("pre_shift_right", lk.pre_shift_right),
        )
        for i, lk in enumerate(lookups)
    ]
    spec = Aggregate(("rules", tables.span("LookupRule", f"{symbol}_rules", rules)))
    return tables.define("LookupSpec", symbol, spec)


def tpl_status_rules(mfct_bits: dict | None) -> list[Lookup]:
    """Driver bits reaching bits 0-4 replace the standard rules, else cover the top three
    (TPL_STS_<dec> without them)."""
    mfct = build_lookup("TPL_STS", "BitToString", mfct_bits) if mfct_bits else None
    if mfct is not None and mfct.mask & 0x1F:
        return [mfct]
    return [
        Lookup("TPL_STS", "IndexToString", HexInt(0x03), "", [
            (HexInt(0), "", TestBit.Set), (HexInt(1), "BUSY", TestBit.Set),
            (HexInt(2), "ERROR", TestBit.Set), (HexInt(3), "ALARM", TestBit.Set),
        ]),
        Lookup("TPL_STS", "BitToString", HexInt(0x1C), "", [
            (HexInt(0x04), "POWER_LOW", TestBit.Set), (HexInt(0x08), "PERMANENT_ERROR", TestBit.Set),
            (HexInt(0x10), "TEMPORARY_ERROR", TestBit.Set),
        ]),
        replace(mfct, mask=HexInt(mfct.mask & 0xE0)) if mfct
        else Lookup("TPL_STS", "BitToString", HexInt(0xE0), "", []),
    ]


def render_decoders(tables: Tables, driver: dict) -> list[Expression]:
    """One per ixml field; the records a decoder derives are what the fields match."""
    rows = []
    for index, d in enumerate(declared_fields(driver)):
        if "ixml" not in d:
            continue
        transform = std_ns.nullopt
        # transform_payload = tpl_aes_cbc_iv,offset,length,tpl_acc_offset
        if text := d.get("transform_payload"):
            _, *args = (part.strip() for part in text.split(","))
            offset, length, tpl_acc_offset = (int(a) for a in args) if args else (0, 0, 0)
            transform = StructInitializer(
                wmbus_ns.struct("PayloadTransform"),
                ("offset", offset), ("length", length), ("tpl_acc_offset", tpl_acc_offset),
            )
        scope = "Frame" if d.get("match_entire_frame") else "Payload" if d.get("match_entire_payload") else "Entry"
        rows.append(Aggregate(
            ("name", d["name"]),
            ("grammar", compile_to_tables(d["ixml"], tables, f"ixml_{driver_symbol(driver['name'])}_{index}")),
            ("scope", enumerator("MatchScope", scope)),
            ("matcher", matcher_spec(tables, build_matcher(d.get("match")))),
            ("transform", transform),
            ("required", "REQUIRED" in d["attributes"]),
        ))
    return rows


def render_fields(tables: Tables, fields: list[Expanded]) -> list[Expression]:
    # A definition expanded into several fields shares one lookup and one formula.
    lookups: dict[int, Expression] = {}
    calculators: dict[int, Expression] = {}
    any_factors: dict[Unit, Expression] = {}

    # The field stored in the formula's unit wins over one of the same quantity,
    # so enercal's total_kwh and total_m3 stay apart.
    by_unit: dict[tuple[str, Unit], Expanded] = {}
    by_quantity: dict[tuple[str, Quantity], Expanded] = {}
    for f in fields:
        if f.definition.kind is FieldKind.Numeric and "{" not in f.definition.name:
            by_unit.setdefault((f.name, f.definition.display_unit), f)
            by_quantity.setdefault((f.name, f.definition.quantity), f)

    def resolve(vname: str, unit: Unit) -> tuple[int, Unit]:
        ref = by_unit.get((vname, unit)) or by_quantity[(vname, UNITS[unit].quantity)]
        return ref.slot, ref.definition.display_unit

    rows = []
    for index, f in enumerate(fields):
        d, m = f.definition, f.matcher
        if d.lookup is not None and id(d) not in lookups:
            lookups[id(d)] = lookup_spec(tables, f"lookup_{index}", [d.lookup])
        if d.calculate is not None and id(d) not in calculators:
            compiled = F.compile_formula(d.calculate, resolve, d.display_unit)
            calculators[id(d)] = tables.formula(f"calculate_{index}", compiled)

        # vif_scale yields the range's unit; the factor to the display unit folds
        # into scale, or per record from a table for an Any* range.
        scale, factors = d.scale, ArrayInitializer()
        if d.source_unit is not None:
            scale *= unit_factor(d.source_unit, d.display_unit)
        elif d.kind is FieldKind.Numeric and m.active and not d.calculate and not m.dif_vif_key:
            if m.vif_range in ANY_RANGE_MEMBERS:
                if d.display_unit not in any_factors:
                    rows_for_unit = []
                    for r in ANY_RANGE_MEMBERS[m.vif_range]:
                        factor = unit_factor(range_unit(r), d.display_unit)
                        if not math.isnan(factor):
                            rows_for_unit.append(
                                Aggregate(("range", span_row(vif_spans(r)[0])), ("factor", double(factor)))
                            )
                    symbol = f"any_{UNITS[d.display_unit].symbol}"
                    any_factors[d.display_unit] = tables.span("RangeFactor", symbol, rows_for_unit)
                factors = any_factors[d.display_unit]
            elif m.vif_range is not VifRange.Any and d.quantity is not Quantity.PointInTime:
                scale *= unit_factor(range_unit(m.vif_range), d.display_unit)

        role = "Holds" if "STATUS" in d.attributes else "Injects" if "INJECT_INTO_STATUS" in d.attributes else "None"
        json_date = JSON_DATES.get(d.display_unit) if d.kind is FieldKind.Numeric else None
        rows.append(Aggregate(
            ("name", f.json_name),
            ("kind", enumerator("FieldKind", d.kind)),
            ("vif_scaling", enumerator("VifScaling", d.vif_scaling)),
            ("dif_signedness", enumerator("DifSignedness", d.dif_signedness)),
            ("status_role", enumerator("StatusRole", role)),
            ("include_tpl_status", "INCLUDE_TPL_STATUS" in d.attributes),
            ("scale", double(scale)),
            ("any_factors", factors),
            ("null_value", std_ns.nullopt if d.null_value is None else double(d.null_value)),
            ("matcher", matcher_spec(tables, m)),
            ("extract_all_matches", f.extract_all),
            ("lookup", lookups.get(id(d), nullptr)),
            ("date_record", d.kind is FieldKind.Numeric and m.vif_range in DATE_RANGES),
            ("json_date", enumerator("TimeFormat", json_date) if json_date else std_ns.nullopt),
            ("text_form", enumerator("TextForm", d.text_form)),
            ("calculate", calculators.get(id(d), nullptr)),
            ("slot", f.slot),
        ))
    return rows


def driver_symbol(name: str) -> str:
    return re.sub(r"[^0-9A-Za-z_]", "_", name)


def generate_from_definition(driver: dict, origin: str, include_fields: set[str] | None) -> str:
    tables = Tables()
    fields = select_fields(concrete_fields(driver), include_fields)
    slots: dict[tuple, int] = {}
    for f in fields:
        # Same kind and JSON name share a slot, as they shared a key in upstream's value map.
        f.slot = slots.setdefault((f.definition.kind, f.json_name), len(slots))

    # A decoder is generated whatever the selection: its records are what the fields match.
    decoder_rows = render_decoders(tables, driver)
    field_rows = render_fields(tables, fields)
    compact = [
        tables.span("uint8_t", f"compact_format_{i}", [HexInt(b) for b in bytes.fromhex(difvif)])
        for i, difvif in enumerate(driver.get("compact_frame_formats", {}).get("difvif", []))
    ]
    tpl_status = nullptr
    if any("INCLUDE_TPL_STATUS" in f.definition.attributes for f in fields):
        tpl_status = lookup_spec(tables, "tpl_status", tpl_status_rules(driver.get("mfct_tpl_status_bits")))

    # The spans are taken first, so the tables are declared in this order.
    fields_span = tables.span("FieldSpec", "fields", field_rows)
    decoders_span = tables.span("DecoderSpec", "decoders", decoder_rows)
    compact_formats = tables.span("std::span<const uint8_t>", "compact_formats", compact)
    detect = tables.span("DetectSpec", "detect", [
        Aggregate(("mfct", mfct), ("version", version), ("type", type_))
        for mfct, version, type_ in driver["detect"]["mvt"]
    ])

    quirk = driver.get("transform_payload")
    spec = Aggregate(
        ("name", driver["name"]),
        ("detect", detect),
        ("fields", fields_span),
        ("decoders", decoders_span),
        ("value_slots", len(slots)),
        ("compact_formats", compact_formats),
        ("tpl_status", tpl_status),
        ("payload_quirk", enumerator("PayloadQuirk", PAYLOAD_QUIRKS[quirk]) if quirk else std_ns.nullopt),
        ("force_media_type", driver.get("force_media_type", nullptr)),
    )
    return TEMPLATES.get_template("driver.cpp.j2").render(
        origin=origin, symbol=driver_symbol(driver["name"]), declarations=tables.declarations, spec=spec
    )
