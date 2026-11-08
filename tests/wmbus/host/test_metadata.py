"""Home Assistant metadata derived from the real driver definitions."""

import sys
from dataclasses import replace
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "components"))

from wmbus_common.drivers.loader import DriverManager  # noqa: E402
from wmbus_common.drivers.loader.metadata import FieldMeta, driver_rows  # noqa: E402

# One field per rule in metadata.py, without upstream's info text; None: left out.
CASES = {
    "volume-noun": ("iperl", "total_m3", FieldMeta("Total water", "m³", "water", "total_increasing", 3, primary=True)),
    "snapshot-at": (
        "hydrus",
        "total_at_date_m3",
        FieldMeta("Total water at date", "m³", "water", "", 3, primary=True),
    ),
    "snapshot-target": ("aptmbusna", "target_m3", FieldMeta("Target water", "m³", "water", "", 3, primary=True)),
    "snapshot-historic": ("apatorna1", "historic_m3", FieldMeta("Historic water", "m³", "water", "", 3)),
    "snapshot-last": (
        "c5isf",
        "total_energy_consumption_last_month_kwh",
        FieldMeta("Total energy consumption last month", "kWh", "energy", "", 3),
    ),
    "increasing": ("caltose", "consumption_hca", FieldMeta("Consumption", "hca", "", "total_increasing", primary=True)),
    "ha-unit": (
        "abbb23",
        "total_reactive_energy_consumption_kvarh",
        FieldMeta("Total reactive energy consumption", "kvarh", "reactive_energy", "total_increasing", 3),
    ),
    "device-class-alias": ("aerius", "flow_m3h", FieldMeta("Flow", "m³/h", "volume_flow_rate", "measurement", 3)),
    "point-in-time": ("hydrus", "target_datetime", FieldMeta("Target date", device_class="timestamp")),
    "text": ("iperl", "status", FieldMeta("Status", text=True)),
    "hidden": ("apatoreitn", "temp_room_avg_raw_counter", None),
    "deprecated": ("aventieshca", "error_flags", None),
}


@pytest.fixture(scope="module")
def drivers():
    for name in DriverManager.available_drivers:
        DriverManager.request_driver(name)
    return {row.name: row.fields for row in driver_rows()}


@pytest.mark.parametrize(("driver", "field", "expected"), CASES.values(), ids=CASES.keys())
def test_field(drivers, driver, field, expected):
    actual = drivers[driver].get(field)
    assert (actual and replace(actual, info="")) == expected
