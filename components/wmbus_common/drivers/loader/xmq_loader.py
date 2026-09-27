"""Transpiles an XMQ driver to the C++ tables meter.cpp reads.

A key outside the schemas, or a construct the runtime does not implement, raises;
what upstream's own loader checks (references, masks, units) is taken as valid.
"""

from __future__ import annotations

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
from voluptuous import All, Any, Coerce, Optional, Remove, Required, Schema

from . import formula as F
from .cpp import TEMPLATES, Aggregate, Tables, double, wmbus_ns
from .ixml import compile_to_tables
from .units import (
    DEFAULT_UNIT,
    UNITS,
    DifSignedness,
    MeasurementType,
    Quantity,
    Unit,
    VifCombinable,
    VifRange,
    VifScaling,
    table,
    unit_factor,
)
from .xmq import parse_xmq

CSV_LIST = All(str, lambda x: [s.strip() for s in x.split(",") if s.strip()])


def first(value):
    """Upstream reads the first of a repeated key."""
    return value[0] if isinstance(value, list) else value


# XMQ's own vocabularies; every value is also the name of the C++ enumerator.


class TestBit(StrEnum):
    Set = "Set"
    NotSet = "NotSet"

    @classmethod
    def _missing_(cls, value):
        # Upstream reads it with strcasecmp, so both spellings are valid.
        wanted = str(value).strip().casefold()
        return next((bit for bit in cls if bit.casefold() == wanted), None)


class FieldKind(StrEnum):
    Numeric = "Numeric"
    Text = "Text"


class ReadableString(StrEnum):
    Normal = "Normal"
    Reversed = "Reversed"


class TextForm(StrEnum):
    Hex = "Hex"
    Readable = "Readable"
    Reversed = "Reversed"
    Date = "Date"
    DateTime = "DateTime"


class MapType(StrEnum):
    BitToString = "BitToString"
    IndexToString = "IndexToString"
    DecimalsToString = "DecimalsToString"


class Attribute(StrEnum):
    """DEPRECATED and HIDE are read and ignored; HIDE only shapes upstream's JSON."""

    Required = "REQUIRED"
    Deprecated = "DEPRECATED"
    Status = "STATUS"
    IncludeTplStatus = "INCLUDE_TPL_STATUS"
    InjectIntoStatus = "INJECT_INTO_STATUS"
    Hide = "HIDE"


class PayloadQuirk(StrEnum):
    """A driver's transform_payload; the name is the C++ enumerator."""

    DiehlPrios = "diehl_prios"
    Sanxing609B = "buggy_sanxing_609B"
    TryQundisDecode = "try_qundis_decode"


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


def range_unit(vif_range: VifRange) -> Unit:
    return Unit(table("vif_ranges")[vif_range]["unit"])


def vif_spans(vif_range: VifRange) -> list[tuple[int, int]]:
    if vif_range is VifRange.Any:
        return []
    members = ANY_RANGE_MEMBERS.get(vif_range, [vif_range])
    return [(table("vif_ranges")[r]["from"], table("vif_ranges")[r]["to"]) for r in members]


def combinable_span(combinable) -> tuple[int, int]:
    if isinstance(combinable, str):
        span = table("vif_combinables")[VifCombinable(combinable)]
        return span["from"], span["to"]
    return int(combinable), int(combinable)


def text_form(d: "FieldDef") -> TextForm:
    vr = d.matcher.vif_range
    if vr in DATE_RANGES:
        return TextForm(vr)
    if d.readable_string is ReadableString.Reversed:
        return TextForm.Reversed
    if d.readable_string is ReadableString.Normal or vr in READABLE_RANGES:
        return TextForm.Readable
    return TextForm.Hex


def frac(s: str):
    num, _, denom = s.partition("/")
    return float(num) / float(denom)


def base16(value) -> HexInt:
    """Upstream reads these with strtol(s, NULL, 16): "10" is 0x10."""
    if isinstance(value, int):
        return HexInt(value)
    return HexInt(int(value, 16))


MATCHER_SCHEMA = {
    Optional("measurement_type"): Coerce(MeasurementType),
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
# Upstream ignores these beside a field; `change` only feeds Home Assistant discovery.
IGNORED_FIELD_KEYS = {
    Remove("default_message"): str,
    Remove("null_string"): str,
    Remove("change"): str,
    Remove("deprecated_by"): str,
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
        Required("map_type"): Coerce(MapType),
        Required("mask_bits"): base16,
        Optional("default_message", default=""): str,
        Optional("map", default=[]): ensure_list(MAP_SCHEMA),
        Optional("mark_reserved_bits"): boolean,
        Optional("pre_shift_right"): Coerce(int),
    }
)

FIELD_SCHEMA = Schema(
    {
        Required("name"): str,
        Required("quantity"): Coerce(Quantity),
        Optional("info"): str,
        Optional("attributes", default=""): All(CSV_LIST, [Coerce(Attribute)]),
        Optional("display_unit"): Coerce(Unit),
        Optional("override_vif_unit"): Coerce(Unit),
        Optional("match"): MATCHER_SCHEMA,
        Optional("calculate"): str,
        Optional("vif_scaling"): Coerce(VifScaling),
        Optional("dif_signedness"): Coerce(DifSignedness),
        Optional("force_scale"): Any(Coerce(float), frac),
        Optional("null_value"): Coerce(float),
        Optional("lookup"): LOOKUP_SCHEMA,
        Optional("readable_string"): Coerce(ReadableString),
        Optional("transform_payload"): str,
        Optional("ixml"): str,
        Optional("match_entire_payload"): boolean,
        Optional("match_entire_frame"): boolean,
        **IGNORED_FIELD_KEYS,
    }
)

def manufacturer_code(flag: str) -> int:
    a, b, c = (ord(ch) - 64 for ch in flag)
    return a * 1024 + b * 32 + c


def parse_mvt(v: list[str]) -> tuple[HexInt, HexInt, HexInt]:
    """mvt = APA,05,06; * is 0xff, "any"."""
    m, ver, typ = v
    if len(m) == 3 and m.isalpha() and m.isupper():
        mfct = manufacturer_code(m)
    else:
        mfct = int(m, 16)

    def part(value):
        return 0xFF if value.strip() == "*" else int(value, 16)

    return HexInt(mfct), HexInt(part(ver)), HexInt(part(typ))


# Upstream fixes the rule's name and type; XMQ supplies the mask and the maps.
MFCT_TPL_STATUS_SCHEMA = Schema(
    {
        Optional("mask_bits", default=0xFF): base16,
        Optional("default_message", default="OK"): str,
        Optional("map", default=[]): ensure_list(MAP_SCHEMA),
    }
)

COMPACT_FRAME_FORMATS_SCHEMA = Schema({Required("difvif"): ensure_list(str)})


LIBRARY_FILE = Path(__file__).resolve().parents[1] / "library.xmq"


def as_list(val) -> list:
    return val if isinstance(val, list) else ([] if val is None else [val])


@cache
def library_fields() -> dict[str, dict]:
    """By template id and alias, for `library { use = ... }`."""
    raw = parse_xmq(LIBRARY_FILE.read_text(encoding="utf-8"))
    fields = {}
    for template in as_list(raw["library"]["template"]):
        definition = {"quantity": "Text"} | {k: v for k, v in template.items() if k not in ("id", "aliases")}
        for name in [template["id"], *template.get("aliases", "").split(",")]:
            if name.strip():
                fields[name.strip()] = definition
    return fields


def use_names(use: str) -> list[str]:
    """`use = a,b` names two library fields, `use = 'a|help'` one whatever the help says."""
    names, bar, _help = use.partition("|")
    return [names.strip()] if bar else [name.strip() for name in names.split(",") if name.strip()]


def map_key(entry: dict) -> int:
    """Upstream merges inherited map entries by this key."""
    return 1 << int(entry["bit"]) if "bit" in entry else base16(entry["value"])


def inherit_lookup(f: dict, template: dict) -> dict:
    """The field's own lookup keys and map entries win; mark_reserved_bits is not inherited."""
    own = f.get("lookup", {})
    lent = template.get("lookup", {})
    lookup = {k: v for k, v in lent.items() if k not in ("map", "mark_reserved_bits")} | own
    own_map = as_list(own.get("map"))
    mapped = {map_key(m) for m in own_map}
    lookup["map"] = own_map + [m for m in as_list(lent.get("map")) if map_key(m) not in mapped]
    return {k: v for k, v in f.items() if k != "template"} | {"lookup": lookup}


def expand_library_and_templates(data: dict) -> dict:
    driver = data["driver"]
    templates = {t["name"]: t for t in as_list((driver.get("templates") or {}).get("template_field"))}

    fields = []
    for use in as_list((driver.get("library") or {}).get("use")):
        for name in use_names(str(use)):
            fields.append(library_fields()[name])
    for f in as_list((driver.get("fields") or {}).get("field")):
        if "template" in f:
            f = inherit_lookup(f, templates[f["template"]])
        fields.append(f)

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
                Optional("transform_payload"): Coerce(PayloadQuirk),
                Optional("mfct_tpl_status_bits"): MFCT_TPL_STATUS_SCHEMA,
                Optional("compact_frame_formats"): COMPACT_FRAME_FORMATS_SCHEMA,
                Required("default_fields"): str,
                Required("detect"): {Required("mvt"): ensure_list(CSV_LIST, parse_mvt)},
                Optional("fields"): {Required("field"): ensure_list(FIELD_SCHEMA)},
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
    measurement_type: MeasurementType = MeasurementType.Any
    vif_range: VifRange = VifRange.Any
    combinables: list[VifCombinable] = field(default_factory=list)
    combinables_raw: list[HexInt] = field(default_factory=list)
    storage: Range | None = (0, 0)
    tariff: Range | None = (0, 0)
    subunit: Range | None = (0, 0)
    index_nr: int = 1

    def range_of(self, counter: F.Counter) -> Range | None:
        return getattr(self, counter.prefix)

    def expects_multiple(self) -> bool:
        return any(r is not None and r[0] != r[1] for r in (self.storage, self.tariff, self.subunit))

    def counter_range(self, counter: F.Counter) -> range:
        low, high = self.range_of(counter)
        return range(low, high + 1)

    def pinned(self, counter: F.Counter, value: int) -> "Matcher":
        return replace(self, **{counter.prefix: (value, value)})


def build_matcher(match: dict | None) -> Matcher:
    if match is None:
        return Matcher()

    m = Matcher(active=True)

    if key := match.get("difvifkey"):
        # set(DifVifKey) ends the build: the remaining keys are ignored.
        m.dif_vif_key = key
        return m

    m.measurement_type = match.get("measurement_type", MeasurementType.Any)
    m.vif_range = match.get("vif_range", VifRange.Any)
    if index_nr := match.get("index_nr"):
        m.index_nr = index_nr

    for counter in ("storage", "tariff", "subunit"):
        values = match.get(f"{counter}_nr")
        if not values:
            continue
        if len(values) > 1:
            setattr(m, counter, (values[0], values[1]))
        elif values[0] == -1 and counter != "subunit":
            # AnyStorageNr/AnyTariffNr; upstream has no such case for subunit.
            setattr(m, counter, None)
        else:
            setattr(m, counter, (values[0], values[0]))

    m.combinables = list(match.get("add_combinable", []))
    m.combinables_raw = list(match.get("add_combinable_raw", []))
    return m


@dataclass
class Lookup:
    name: str
    map_type: MapType
    mask: HexInt
    default_message: str
    entries: list[tuple[HexInt, str, TestBit]]  # (from, to, test)
    pre_shift_right: int = 0


def map_value(entry: dict) -> HexInt:
    if "bit" in entry:
        return HexInt(1 << entry["bit"])
    return entry["value"]


def build_lookup(name: str, map_type: MapType, lookup: dict) -> Lookup:
    """A mask of 0 means the map's bits."""
    entries = [(map_value(e), e["name"], e["test"]) for e in lookup["map"]]
    covered = reduce(operator.or_, (value for value, *_ in entries), 0)
    mask = lookup["mask_bits"] or covered
    if lookup.get("mark_reserved_bits"):
        entries += [(HexInt(1 << bit), f"RESERVED_BIT_{bit}", TestBit.Set)
                    for bit in range(64) if mask & ~covered & (1 << bit)]
    return Lookup(
        name=name,
        map_type=map_type,
        mask=HexInt(mask),
        default_message=lookup.get("default_message", ""),
        entries=entries,
        pre_shift_right=int(lookup.get("pre_shift_right", 0)),
    )


@dataclass
class FieldDef:
    name: str  # vname; may carry a {template}
    kind: FieldKind
    quantity: Quantity
    display_unit: Unit
    vif_scaling: VifScaling = VifScaling.Auto
    dif_signedness: DifSignedness = DifSignedness.Signed
    attributes: list[Attribute] = field(default_factory=list)
    scale: float = 1.0
    source_unit: Unit | None = None  # override_vif_unit
    null_value: float | None = None
    matcher: Matcher = field(default_factory=Matcher)
    lookup: Lookup | None = None
    readable_string: ReadableString | None = None
    calculate: str | None = None
    # An ixml carrier runs over every record and keeps the last text.
    every_match: bool = False


def field_def(d: dict) -> FieldDef:
    quantity = d["quantity"]
    attributes = d["attributes"]
    match = d.get("match")
    calculate = d.get("calculate")

    if quantity is not Quantity.Text:
        display_unit = d.get("display_unit") or DEFAULT_UNIT[quantity]
        if calculate:
            return FieldDef(
                d["name"], FieldKind.Numeric, quantity, display_unit,
                attributes=attributes, matcher=build_matcher(match), calculate=calculate,
            )
        # override_vif_unit: the record holds this unit whatever its VIF says.
        source_unit = d.get("override_vif_unit")
        if match is not None:
            return FieldDef(
                d["name"], FieldKind.Numeric, quantity, display_unit,
                vif_scaling=VifScaling["None"] if source_unit else d.get("vif_scaling", VifScaling.Auto),
                dif_signedness=d.get("dif_signedness", DifSignedness.Signed),
                attributes=attributes, scale=float(d.get("force_scale", 1.0)), source_unit=source_unit,
                null_value=d.get("null_value"), matcher=build_matcher(match),
            )
        return FieldDef(d["name"], FieldKind.Numeric, quantity, display_unit,
                        vif_scaling=VifScaling["None"], attributes=attributes)

    f = FieldDef(d["name"], FieldKind.Text, quantity, Unit.TXT, vif_scaling=VifScaling["None"],
                 attributes=attributes, matcher=build_matcher(match))
    if lookup := d.get("lookup"):
        f.lookup = build_lookup(lookup["name"], lookup["map_type"], lookup)
    f.readable_string = d.get("readable_string")
    f.every_match = "ixml" in d
    return f


@dataclass
class DecoderDef:
    """An ixml field; the records it derives are what the fields match."""

    name: str
    index: int
    source: str
    scope: str
    matcher: Matcher
    transform: tuple[int, int, int] | None  # transform_payload = tpl_aes_cbc_iv,offset,length,tpl_acc_offset
    required: bool


def decoder_def(d: dict, index: int) -> DecoderDef:
    scope = "Frame" if d.get("match_entire_frame") else "Payload" if d.get("match_entire_payload") else "Entry"
    matcher = build_matcher(d.get("match"))
    transform = None
    if text := d.get("transform_payload"):
        _, *args = (part.strip() for part in text.split(","))
        transform = tuple(int(a) for a in args) if args else (0, 0, 0)
    return DecoderDef(d["name"], index, d["ixml"], scope, matcher, transform, Attribute.Required in d["attributes"])


def declared_fields(driver: dict) -> list[dict]:
    return as_list((driver.get("fields") or {}).get("field"))


def collect_definitions(driver: dict) -> list[FieldDef]:
    """Upstream's registration order; an ixml field over the payload or frame only derives records."""
    return [
        field_def(d) for d in declared_fields(driver)
        if not ("ixml" in d and (d.get("match_entire_payload") or d.get("match_entire_frame")))
    ]


def collect_decoders(driver: dict) -> list[DecoderDef]:
    return [decoder_def(d, index) for index, d in enumerate(declared_fields(driver)) if "ixml" in d]


def select_fields(fields: list[Expanded], include: set[str] | None) -> list[Expanded]:
    """Adds what the selection needs: formula inputs and, for STATUS, the INJECT_INTO_STATUS fields."""
    if include is None:
        return fields

    selected = {i for i, f in enumerate(fields) if f.json_name in include}
    while True:
        wanted = set()
        for i in selected:
            d = fields[i].definition
            if d.calculate:
                for vname, quantity in F.referenced_fields(d.calculate):
                    wanted |= {j for j, e in enumerate(fields)
                               if e.name == vname and e.definition.quantity == quantity}
            if Attribute.Status in d.attributes:
                wanted |= {j for j, e in enumerate(fields)
                           if Attribute.InjectIntoStatus in e.definition.attributes}
        if wanted <= selected:
            break
        selected |= wanted
    return [f for i, f in enumerate(fields) if i in selected]


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


def expand(defs: list[FieldDef]) -> list[Expanded]:
    expanded = []
    for d in defs:
        counters = sorted(F.template_counters(d.name))
        if not counters:
            expanded.append(Expanded(d, d.name, d.matcher, d.every_match or d.matcher.expects_multiple()))
            continue
        ranges = [d.matcher.counter_range(c) for c in counters]
        for values in itertools.product(*ranges):
            env = dict(zip(counters, values))
            matcher = d.matcher
            for counter, value in env.items():
                matcher = matcher.pinned(counter, value)
            expanded.append(Expanded(d, F.expand_template(d.name, env), matcher, d.every_match or d.matcher.expects_multiple()))
    return expanded


def concrete_fields(driver: dict) -> list[Expanded]:
    return expand(collect_definitions(driver))


def assign_slots(fields: list[Expanded]) -> int:
    """Same kind and JSON name share a slot, as they shared a key in upstream's value map."""
    slots: dict[tuple[str, str], int] = {}
    for f in fields:
        f.slot = slots.setdefault((f.definition.kind, f.json_name), len(slots))
    return len(slots)


FIELD_KIND = wmbus_ns.enum("FieldKind", is_class=True)
VIF_SCALING = wmbus_ns.enum("VifScaling", is_class=True)
DIF_SIGNEDNESS = wmbus_ns.enum("DifSignedness", is_class=True)
MEASUREMENT_TYPE = wmbus_ns.enum("MeasurementType", is_class=True)
MATCH_SCOPE = wmbus_ns.enum("MatchScope", is_class=True)
TEXT_FORM = wmbus_ns.enum("TextForm", is_class=True)
TIME_FORMAT = wmbus_ns.enum("TimeFormat", is_class=True)
STATUS_ROLE = wmbus_ns.enum("StatusRole", is_class=True)
TEST_BIT = wmbus_ns.enum("TestBit", is_class=True)
MAP_TYPE = wmbus_ns.enum("MapType", is_class=True)
PAYLOAD_QUIRK = wmbus_ns.enum("PayloadQuirk", is_class=True)
COUNTER_RANGE = wmbus_ns.struct("CounterRange")
PAYLOAD_TRANSFORM = wmbus_ns.struct("PayloadTransform")


def counter_range(m: Matcher, counter: F.Counter) -> Expression:
    """std::optional is no aggregate, so the range names its type."""
    r = m.range_of(counter)
    if r is None:
        return std_ns.nullopt
    return StructInitializer(COUNTER_RANGE, ("from", r[0]), ("to", r[1]))


def span_row(span: tuple[int, int]) -> Expression:
    return Aggregate(("from", HexInt(span[0])), ("to", HexInt(span[1])))


class SpanTables:
    def __init__(self, tables: Tables, prefix: str):
        self.tables = tables
        self.prefix = prefix
        self.made: dict[tuple, Expression] = {}

    def span(self, spans: list[tuple[int, int]]) -> Expression:
        key = tuple(spans)
        if key not in self.made:
            self.made[key] = self.tables.span("VifSpan", f"{self.prefix}_{len(self.made)}", [span_row(s) for s in spans])
        return self.made[key]


def matcher_spec(m: Matcher, ranges: SpanTables, combinables: SpanTables) -> Expression:
    listed = [c for c in m.combinables if c != VifCombinable.Any] + list(m.combinables_raw)
    return Aggregate(
        ("dif_vif_key", m.dif_vif_key),
        ("combinables", combinables.span([combinable_span(c) for c in listed])),
        ("any_combinable", VifCombinable.Any in m.combinables),
        ("measurement_type", getattr(MEASUREMENT_TYPE, m.measurement_type)),
        ("vif_ranges", ranges.span(vif_spans(m.vif_range))),
        ("storage", counter_range(m, F.Counter.STORAGE_COUNTER)),
        ("tariff", counter_range(m, F.Counter.TARIFF_COUNTER)),
        ("subunit", counter_range(m, F.Counter.SUBUNIT_COUNTER)),
        ("index_nr", m.index_nr),
        ("active", m.active),
    )


def lookup_spec(tables: Tables, symbol: str, lookups: list[Lookup]) -> Expression:
    rules = [
        Aggregate(
            ("name", lk.name),
            ("type", getattr(MAP_TYPE, lk.map_type)),
            ("mask", lk.mask),
            ("default_message", lk.default_message),
            ("map", tables.span("LookupMap", f"{symbol}_map_{i}", [
                Aggregate(("from", frm), ("to", to), ("test", getattr(TEST_BIT, test)))
                for frm, to, test in lk.entries
            ])),
            ("pre_shift_right", lk.pre_shift_right),
        )
        for i, lk in enumerate(lookups)
    ]
    spec = Aggregate(("rules", tables.span("LookupRule", f"{symbol}_rules", rules)))
    return tables.define("LookupSpec", symbol, spec)


def tpl_status_rules(mfct_bits: dict | None) -> list[Lookup]:
    """Driver bits reaching bits 0-4 replace the standard rules, else cover the top three (UNKNOWN_XX without them)."""
    mfct = build_lookup("TPL_STS", MapType.BitToString, mfct_bits) if mfct_bits else None
    if mfct is not None and mfct.mask & 0x1F:
        return [mfct]
    return [
        Lookup("TPL_STS", MapType.IndexToString, HexInt(0x03), "", [
            (HexInt(0), "", TestBit.Set), (HexInt(1), "BUSY", TestBit.Set),
            (HexInt(2), "ERROR", TestBit.Set), (HexInt(3), "ALARM", TestBit.Set),
        ]),
        Lookup("TPL_STS", MapType.BitToString, HexInt(0x1C), "", [
            (HexInt(0x04), "POWER_LOW", TestBit.Set), (HexInt(0x08), "PERMANENT_ERROR", TestBit.Set),
            (HexInt(0x10), "TEMPORARY_ERROR", TestBit.Set),
        ]),
        replace(mfct, mask=HexInt(mfct.mask & 0xE0)) if mfct else Lookup("UNKNOWN", MapType.BitToString, HexInt(0xE0), "", []),
    ]


def json_date(d: FieldDef) -> Expression:
    """Upstream's JSON writes a numeric field in a date unit as text."""
    match d.display_unit if d.kind is FieldKind.Numeric else None:
        case Unit.DateLT:
            return TIME_FORMAT.Date
        case Unit.DateTimeLT:
            return TIME_FORMAT.DateTime
        case Unit.DateTimeUTC:
            return TIME_FORMAT.TimestampUTC
    return std_ns.nullopt


def status_role(attributes: list[Attribute]) -> Expression:
    if Attribute.Status in attributes:
        return STATUS_ROLE.Holds
    if Attribute.InjectIntoStatus in attributes:
        return STATUS_ROLE.Injects
    return getattr(STATUS_ROLE, "None")


def render_decoders(tables: Tables, driver_name: str, decoders: list[DecoderDef],
                    ranges: SpanTables, combinables: SpanTables) -> list[Expression]:
    rows = []
    for d in decoders:
        transform = std_ns.nullopt
        if d.transform:
            offset, length, tpl_acc_offset = d.transform
            transform = StructInitializer(
                PAYLOAD_TRANSFORM, ("offset", offset), ("length", length), ("tpl_acc_offset", tpl_acc_offset)
            )
        rows.append(Aggregate(
            ("name", d.name),
            ("grammar", compile_to_tables(d.source, tables, f"ixml_{driver_symbol(driver_name)}_{d.index}")),
            ("scope", getattr(MATCH_SCOPE, d.scope)),
            ("matcher", matcher_spec(d.matcher, ranges, combinables)),
            ("transform", transform),
            ("required", d.required),
        ))
    return rows


def render_fields(tables: Tables, fields: list[Expanded], ranges: SpanTables,
                  combinables: SpanTables) -> list[Expression]:
    rows = []
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

    def resolve(vname: str, unit: Unit) -> F.FieldRef:
        ref = by_unit.get((vname, unit)) or by_quantity[(vname, UNITS[unit].quantity)]
        return F.FieldRef(ref.slot, ref.definition.display_unit)

    for index, f in enumerate(fields):
        d = f.definition

        # A definition expanded into several fields shares one lookup and one formula.
        lookup = nullptr
        if d.lookup is not None:
            if id(d) not in lookups:
                lookups[id(d)] = lookup_spec(tables, f"lookup_{index}", [d.lookup])
            lookup = lookups[id(d)]

        calculate = nullptr
        if d.calculate is not None:
            if id(d) not in calculators:
                compiled = F.compile_formula(d.calculate, resolve, d.display_unit)
                calculators[id(d)] = tables.formula(f"calculate_{index}", compiled)
            calculate = calculators[id(d)]

        # vif_scale yields the range's unit; the factor to the display unit folds
        # into scale, or per record from a table for an Any* range.
        scale = d.scale
        factors = ArrayInitializer()
        vr = f.matcher.vif_range
        if d.source_unit is not None:
            scale *= unit_factor(d.source_unit, d.display_unit)
        elif d.kind is FieldKind.Numeric and f.matcher.active and not d.calculate and not f.matcher.dif_vif_key:
            if vr in ANY_RANGE_MEMBERS:
                if d.display_unit not in any_factors:
                    rows_for_unit = []
                    for r in ANY_RANGE_MEMBERS[vr]:
                        factor = unit_factor(range_unit(r), d.display_unit)
                        if not math.isnan(factor):
                            rows_for_unit.append(Aggregate(("range", span_row(vif_spans(r)[0])), ("factor", double(factor))))
                    any_factors[d.display_unit] = tables.span("RangeFactor", f"any_{UNITS[d.display_unit].symbol}", rows_for_unit)
                factors = any_factors[d.display_unit]
            elif vr is not VifRange.Any and d.quantity is not Quantity.PointInTime:
                scale *= unit_factor(range_unit(vr), d.display_unit)

        rows.append(Aggregate(
            ("name", f.json_name),
            ("kind", getattr(FIELD_KIND, d.kind)),
            ("vif_scaling", getattr(VIF_SCALING, d.vif_scaling)),
            ("dif_signedness", getattr(DIF_SIGNEDNESS, d.dif_signedness)),
            ("status_role", status_role(d.attributes)),
            ("include_tpl_status", Attribute.IncludeTplStatus in d.attributes),
            ("scale", double(scale)),
            ("any_factors", factors),
            ("null_value", std_ns.nullopt if d.null_value is None else double(d.null_value)),
            ("matcher", matcher_spec(f.matcher, ranges, combinables)),
            ("extract_all_matches", f.extract_all),
            ("lookup", lookup),
            ("date_record", d.kind is FieldKind.Numeric and f.matcher.vif_range in DATE_RANGES),
            ("json_date", json_date(d)),
            ("text_form", getattr(TEXT_FORM, text_form(d) if d.kind is FieldKind.Text else TextForm.Hex)),
            ("calculate", calculate),
            ("slot", f.slot),
        ))

    return rows


def driver_symbol(name: str) -> str:
    return re.sub(r"[^0-9A-Za-z_]", "_", name)


def generate_from_definition(driver: dict, origin: str, include_fields: set[str] | None) -> str:
    tables = Tables()
    expanded = select_fields(concrete_fields(driver), include_fields)
    value_slots = assign_slots(expanded)

    # A decoder is generated whatever the selection: its records are what the fields match.
    ranges = SpanTables(tables, "vif_ranges")
    combinables = SpanTables(tables, "combinables")
    decoder_rows = render_decoders(tables, driver["name"], collect_decoders(driver), ranges, combinables)
    rows = render_fields(tables, expanded, ranges, combinables)

    compact = []
    if formats := driver.get("compact_frame_formats"):
        compact = [
            tables.span("uint8_t", f"compact_format_{i}", [HexInt(b) for b in bytes.fromhex(difvif)])
            for i, difvif in enumerate(formats["difvif"])
        ]

    tpl_status = nullptr
    if any(Attribute.IncludeTplStatus in f.definition.attributes for f in expanded):
        tpl_status = lookup_spec(tables, "tpl_status", tpl_status_rules(driver.get("mfct_tpl_status_bits")))

    # The spans are taken first, so the tables are declared in this order.
    fields = tables.span("FieldSpec", "fields", rows)
    decoders = tables.span("DecoderSpec", "decoders", decoder_rows)
    compact_formats = tables.span("std::span<const uint8_t>", "compact_formats", compact)
    detect = tables.span("DetectSpec", "detect", [
        Aggregate(("mfct", mfct), ("version", version), ("type", type_))
        for mfct, version, type_ in driver["detect"]["mvt"]
    ])

    quirk = driver.get("transform_payload")
    spec = Aggregate(
        ("name", driver["name"]),
        ("detect", detect),
        ("fields", fields),
        ("decoders", decoders),
        ("value_slots", value_slots),
        ("compact_formats", compact_formats),
        ("tpl_status", tpl_status),
        ("payload_quirk", getattr(PAYLOAD_QUIRK, quirk.name) if quirk else std_ns.nullopt),
        ("force_media_type", driver.get("force_media_type", nullptr)),
    )

    return TEMPLATES.get_template("driver.cpp.j2").render(
        origin=origin, symbol=driver_symbol(driver["name"]), declarations=tables.declarations, spec=spec
    )

