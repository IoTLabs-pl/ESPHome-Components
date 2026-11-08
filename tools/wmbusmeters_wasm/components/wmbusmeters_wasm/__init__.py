import json
from dataclasses import asdict
from pathlib import Path

import jinja2

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


def _render(name, **context):
    template = jinja2.Template(Path(__file__).with_name(f"{name}.j2").read_text(), keep_trailing_newline=True)
    # Unchanged files keep their time, so CMake relinks only on a change.
    write_file_if_changed(CORE.relative_build_path(name), template.render(context))


async def to_code(config):
    version = config[CONF_VERSION]
    source = f"https://github.com/IoTLabs-pl/ESPHome-Components/tree/{version}/tools/wmbusmeters_wasm"
    _render("banner.js", version=version, source=source, upstream_ref=UPSTREAM_REF)
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
    _render("metadata.js", metadata=json.dumps(metadata, ensure_ascii=False, separators=(",", ":")))
