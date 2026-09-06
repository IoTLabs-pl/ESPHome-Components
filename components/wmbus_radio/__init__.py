from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

from . import transceiver_sx1262, transceiver_sx1276
from .radio import SCHEMA as RADIO_SCHEMA
from .radio import RadioComponent, register_radio
from .transceiver import CONF_RADIO_TYPE

__all__ = ["RadioComponent"]

CODEOWNERS = ["@kubasaw"]
DEPENDENCIES = ["esp32", "spi"]
AUTO_LOAD = ["wmbus_common"]
MULTI_CONF = True


TRANSCEIVERS = {
    module.__name__.split(".")[-1].removeprefix("transceiver_").upper(): module
    for module in (transceiver_sx1262, transceiver_sx1276)
}


CONFIG_SCHEMA = cv.typed_schema(
    {
        name: module.config_schema(name, RADIO_SCHEMA)
        for name, module in TRANSCEIVERS.items()
    },
    key=CONF_RADIO_TYPE,
    upper=True,
)


def FILTER_SOURCE_FILES():
    used = {config[CONF_RADIO_TYPE] for config in CORE.config["wmbus_radio"]}
    return [
        Path(module.__file__).with_suffix(suffix).name
        for name, module in TRANSCEIVERS.items()
        if name not in used
        for suffix in (".cpp", ".h")
    ]


async def to_code(config):
    trx_var = await TRANSCEIVERS[config[CONF_RADIO_TYPE]].to_code(config)
    radio_var = await register_radio(config)

    cg.add(radio_var.set_transceiver(trx_var))
