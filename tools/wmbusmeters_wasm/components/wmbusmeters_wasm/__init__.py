import json
from dataclasses import asdict

import esphome.config_validation as cv
from esphome.components.wmbus_common import UPSTREAM_REF
from esphome.components.wmbus_common.drivers.loader.metadata import driver_rows
from esphome.const import CONF_VERSION
from esphome.core import CORE
from esphome.helpers import write_file_if_changed

DEPENDENCIES = ["wmbus_common"]

CONFIG_SCHEMA = cv.Schema({cv.Required(CONF_VERSION): cv.string})

BANNER = """\
// wmbusmeters_wasm {version}, GPL-3.0, source: {source}
// Contains wmbusmeters {upstream_ref} (GPL-3.0), ESPHome (GPL-3.0), ArduinoJson (MIT, (c) Benoit Blanchon) and Mbed TLS (GPL-2.0-or-later).
"""
METADATA = "Object.assign(Module,{metadata});\n"


def _present(entry):
    return {key: value for key, value in entry.items() if value}


def _write(name, content):
    # Unchanged files keep their time, so CMake relinks only on a change.
    write_file_if_changed(CORE.relative_build_path(name), content)


async def to_code(config):
    version = config[CONF_VERSION]
    source = f"https://github.com/IoTLabs-pl/ESPHome-Components/tree/{version}/tools/wmbusmeters_wasm"
    _write("banner.js", BANNER.format(version=version, source=source, upstream_ref=UPSTREAM_REF))
    metadata = {
        "source": source,
        "drivers": {
            driver.name: {
                **_present({"manufacturer": driver.manufacturer, "model": driver.model}),
                "fields": {name: _present(asdict(field)) for name, field in driver.fields.items()},
            }
            for driver in driver_rows()
        },
    }
    _write("metadata.js", METADATA.format(metadata=json.dumps(metadata, ensure_ascii=False, separators=(",", ":"))))
