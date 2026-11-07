"""Home Assistant metadata derived from the real driver definitions."""

import sys
from dataclasses import astuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "components"))

from wmbus_common.drivers.loader import DriverManager  # noqa: E402
from wmbus_common.drivers.loader.metadata import FieldMeta, driver_rows  # noqa: E402


def test_driver_rows():
    for name in DriverManager.available_drivers:
        DriverManager.request_driver(name)
    rows = driver_rows()
    assert [row.name for row in rows] == [driver.name for driver in DriverManager.registered_drivers]
    drivers = {row.name: row for row in rows}
    iperl = drivers["iperl"]
    assert (iperl.manufacturer, iperl.model) == ("Sensus", "Sensus iPERL")
    assert iperl.fields["total_m3"] == FieldMeta(
        "Total water", "m³", "water", "total_increasing", 3, True, False, "The total water consumption."
    )
    assert iperl.fields["status"].text

    hydrus = drivers["hydrus"].fields
    assert astuple(hydrus["target_m3"])[:6] == ("Target water", "m³", "water", "", 3, False)
    assert astuple(hydrus["target_datetime"])[:6] == ("Target date", "", "timestamp", "", 0, False)
    assert astuple(hydrus["flow_temperature_c"])[:6] == ("Flow temperature", "°C", "temperature", "measurement", 2, False)
    assert astuple(drivers["abbb23"].fields["total_reactive_energy_consumption_kvarh"])[:6] == (
        "Total reactive energy consumption", "kvarh", "reactive_energy", "total_increasing", 3, False
    )
    assert "temp_room_avg_raw_counter" not in drivers["apatoreitn"].fields
    assert "error_flags" not in drivers["aventieshca"].fields
