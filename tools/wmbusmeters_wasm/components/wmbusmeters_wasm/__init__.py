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


def _present(entry):
    return {key: value for key, value in entry.items() if value}


async def to_code(config):
    version = config[CONF_VERSION]
    source = f"https://github.com/IoTLabs-pl/ESPHome-Components/tree/{version}/tools/wmbusmeters_wasm"
    # Unchanged files keep their time, so CMake relinks only on a change.
    write_file_if_changed(
        CORE.relative_build_path("banner.js"),
        f"// wmbusmeters_wasm {version}, GPL-3.0, source: {source}\n"
        f"// Contains wmbusmeters {UPSTREAM_REF} (GPL-3.0), ArduinoJson (MIT, (c) Benoit Blanchon)"
        " and Mbed TLS (GPL-2.0-or-later).\n",
    )
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
    write_file_if_changed(
        CORE.relative_build_path("metadata.js"),
        f"Object.assign(Module,{json.dumps(metadata, ensure_ascii=False, separators=(',', ':'))});\n",
    )
