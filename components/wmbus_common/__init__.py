from pathlib import Path

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.const import CONF_ID, CONF_NAME, SOURCE_FILE_EXTENSIONS
from esphome.core import CORE
from esphome.yaml_util import ESPHomeDumper

from .driver_loader import CppDriver, Driver, DriverManager

CODEOWNERS = ["@kubasaw"]
CONF_ALL = "all"
CONF_DRIVERS = "drivers"
CONF_FIELDS = "fields"

CURRENT_DIR = Path(__file__).parent
SOURCE_FILE_EXTENSIONS.add(".cc")


wmbus_common_ns = cg.esphome_ns.namespace("wmbus_common")
WMBusCommon = wmbus_common_ns.class_("WMBusCommon", cg.Component)


ESPHomeDumper.add_multi_representer(
    Driver, lambda s, v: s.represent_stringify(s, v.name)
)


def maybe_all(replacements):
    def validator(v):
        if v == CONF_ALL:
            return replacements
        else:
            return v

    return validator


def driver_field_validator(conf):
    driver = conf[CONF_NAME]

    conf[CONF_FIELDS] = cv.All(
        maybe_all(driver.available_fields),
        [cv.All(cv.one_of(*driver.available_fields), driver.request_field)],
    )(conf[CONF_FIELDS])

    return conf


DRIVER_ENTRY_SCHEMA = cv.maybe_simple_value(
    {
        cv.Required(CONF_NAME): cv.All(
            cv.one_of(*DriverManager.available_drivers), DriverManager.request_driver
        ),
        cv.Optional(CONF_FIELDS, default=CONF_ALL): cv.valid,
    },
    driver_field_validator,
    key=CONF_NAME,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WMBusCommon),
        cv.Optional(CONF_DRIVERS, default=[]): cv.All(
            maybe_all(DriverManager.available_drivers), [DRIVER_ENTRY_SCHEMA]
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
        for d in DriverManager._all_drivers.values()
        if isinstance(d, CppDriver)
    ]
