"""How Home Assistant should present each driver field: label, unit, device class, state class and decimals."""

from dataclasses import dataclass

import esphome.const as const

from . import DriverManager
from .units import UNITS, Unit
from .xmq_loader import FieldKind

# The resolution meters commonly send each unit in; any other unit is a whole number.
DEFAULT_ACCURACY_DECIMALS = {
    Unit.M3: 3,
    Unit.M3H: 3,
    Unit.KWH: 3,
    Unit.KVARH: 3,
    Unit.KVAH: 3,
    Unit.KW: 3,
    Unit.KVAR: 3,
    Unit.KVA: 3,
    Unit.Ampere: 3,
    Unit.FACTOR: 3,
    Unit.C: 2,
    Unit.Volt: 2,
    Unit.Hertz: 2,
    Unit.BAR: 2,
    Unit.RH: 1,
    Unit.DEGREE: 1,
    Unit.Year: 1,
}

# Home Assistant's names, as ESPHome declares them.
HA_DEVICE_CLASSES = {value for key, value in vars(const).items() if key.startswith("DEVICE_CLASS_")}
HA_UNITS = {value.lower(): value for key, value in vars(const).items() if key.startswith("UNIT_")}
# Where Home Assistant's name is not upstream's quantity or unit in lower case.
DEVICE_CLASS_ALIASES = {"Flow": "volume_flow_rate", "Amperage": "current", "RelativeHumidity": "humidity"}
UNIT_ALIASES = {Unit.RH: "%", Unit.PERCENTAGE: "%"}
CUMULATIVE = {"Energy", "Reactive_Energy", "Apparent_Energy", "Volume", "HCA"}
VOLUME = {"WaterMeter": {"m3": "water", "l": "water"}, "GasMeter": {"m3": "gas"}}
TIME_DEVICE_CLASSES = {
    "date": "date",
    "datetime": "timestamp",
    "utc": "timestamp",
    "Date": "date",
    "DateTime": "timestamp",
}
# What a field measures, for a label that does not say it: "total_tariff1" is "Total water tariff1".
QUANTITY_NOUNS = {
    "Energy": "energy",
    "Reactive_Energy": "energy",
    "Apparent_Energy": "energy",
    "Power": "power",
    "Flow": "flow",
    "Temperature": "temperature",
    "Time": "time",
    "HCA": "units",
    "PointInTime": "date",
}
VOLUME_NOUNS = {"WaterMeter": "water", "GasMeter": "gas"}
# A cumulative value read out at some past moment: statistics of a total would count its jumps as consumption.
SNAPSHOT_MARKERS = ("_at_", "history", "prev", "target", "set_date", "ago", "delta", "month", "last_")


@dataclass(frozen=True)
class FieldMeta:
    label: str
    unit: str = ""
    device_class: str = ""
    state_class: str = ""
    decimals: int = 0
    primary: bool = False
    text: bool = False
    info: str = ""


@dataclass(frozen=True)
class DriverMeta:
    name: str
    manufacturer: str | None
    model: str | None
    fields: dict[str, FieldMeta]


def _device_class(quantity):
    return DEVICE_CLASS_ALIASES.get(quantity) or (quantity.lower() if quantity.lower() in HA_DEVICE_CLASSES else "")


def _unit(unit):
    return UNIT_ALIASES.get(unit) or HA_UNITS.get(UNITS[unit].display.lower(), UNITS[unit].display)


def _state_class(vname, quantity, change):
    if quantity in CUMULATIVE:
        live = vname.startswith(("total", "current_consumption")) or change == "Increasing"
        return "total_increasing" if live and not any(m in vname for m in SNAPSHOT_MARKERS) else ""
    return "measurement" if _device_class(quantity) else ""


def _label(vname, quantity, meter_type):
    words = vname.split("_")
    noun = VOLUME_NOUNS.get(meter_type, "volume") if quantity == "Volume" else QUANTITY_NOUNS.get(quantity)
    if noun and noun not in words and "consumption" not in words:
        if len(words) == 1:
            words.append(noun)
        elif words[0] in ("total", "target"):
            words.insert(1, noun)
    return " ".join(words).capitalize()


def _field(f, driver):
    d = f.definition
    common = {"primary": f.json_name in driver.default_fields, "info": d.info}
    if d.kind is FieldKind.Text:
        label = f.json_name.replace("_", " ").capitalize()
        return FieldMeta(label, device_class=TIME_DEVICE_CLASSES.get(str(d.text_form), ""), text=True, **common)
    quantity = str(d.quantity)
    label = _label(f.name, quantity, driver.meter_type)
    symbol = UNITS[d.display_unit].symbol
    if symbol in TIME_DEVICE_CLASSES:
        return FieldMeta(label, device_class=TIME_DEVICE_CLASSES[symbol], **common)
    return FieldMeta(
        label,
        _unit(d.display_unit),
        VOLUME.get(driver.meter_type, {}).get(symbol, "volume") if symbol in ("m3", "l") else _device_class(quantity),
        _state_class(f.name, quantity, d.change),
        DEFAULT_ACCURACY_DECIMALS.get(d.display_unit, 0),
        **common,
    )


def driver_rows():
    """DriverMeta per registered driver, in registry order; a field's info is upstream's description."""
    rows = []
    for driver in DriverManager.registered_drivers:
        fields = {}
        for f in driver.concrete_fields:
            if f.json_name not in fields and "HIDE" not in f.definition.attributes and not f.definition.deprecated_by:
                fields[f.json_name] = _field(f, driver)
        rows.append(DriverMeta(driver.name, driver.manufacturer, driver.model, fields))
    return rows
