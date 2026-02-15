from pathlib import Path

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.const import CONF_ID
from esphome.core import CORE

from .driver_loader import CppDriver, DriverManager

CODEOWNERS = ["@kubasaw"]
CONF_DRIVERS = "drivers"
CONF_MARK_ALL = "all"

wmbus_common_ns = cg.esphome_ns.namespace("wmbus_common")
WMBusCommon = wmbus_common_ns.class_("WMBusCommon", cg.Component)

CURRENT_DIR = Path(__file__).parent

driver_validator = cv.All(
    cv.one_of(*sorted(DriverManager.all_drivers), lower=True, space="_"),
    DriverManager.request_driver,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WMBusCommon),
        cv.Optional(CONF_DRIVERS): cv.All(
            lambda x: list(DriverManager.all_drivers) if x == CONF_MARK_ALL else x,
            [driver_validator],
        ),
    }
)


async def to_code(config):
    cg.add_define(
        "WMBUSMETERS_TAG",
        CURRENT_DIR.joinpath(".wmbusmeters_tag").read_text(),
    )

    target_dir = CORE.relative_src_path("wmbusmeters_drivers")

    DriverManager.sync_to_directory(target_dir)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)


def FILTER_SOURCE_FILES() -> list[str]:
    return [
        d.source_path.name
        for d in DriverManager.all_drivers.values()
        if isinstance(d, CppDriver)
    ]
