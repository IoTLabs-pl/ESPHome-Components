import esphome.config_validation as cv
from esphome.const import SOURCE_FILE_EXTENSIONS, CONF_ID
from esphome import codegen as cg
from pathlib import Path

CODEOWNERS = ["@kubasaw"]
CONF_DRIVERS = "drivers"

wmbus_common_ns = cg.esphome_ns.namespace("wmbus_common")
WMBusCommon = wmbus_common_ns.class_("WMBusCommon", cg.Component)

SOURCE_FILE_EXTENSIONS.add(".cc")

_ALWAYS_EXCLUDED_DRIVERS = {"auto", "unknown"}

AVAILABLE_DRIVERS = set(
    name
    for f in Path(__file__).parent.glob("driver_*.cc")
    if (name := f.stem.removeprefix("driver_")) not in _ALWAYS_EXCLUDED_DRIVERS
)

_registered_drivers = set()


validate_driver = cv.All(
    cv.one_of(*sorted(AVAILABLE_DRIVERS), lower=True, space="_"),
    lambda driver: _registered_drivers.add(driver) or driver,
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WMBusCommon),
        cv.Optional(CONF_DRIVERS): cv.All(
            lambda x: list(AVAILABLE_DRIVERS) if x == "all" else x,
            [validate_driver],
        ),
    }
)


async def to_code(config):
    cg.add_define(
        "WMBUSMETERS_TAG", Path(__file__).with_name(".wmbusmeters_tag").read_text()
    )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)


def FILTER_SOURCE_FILES() -> list[str]:
    excluded_driver_names = _ALWAYS_EXCLUDED_DRIVERS | (
        AVAILABLE_DRIVERS - _registered_drivers
    )

    return [f"driver_{name}.cc" for name in excluded_driver_names]
