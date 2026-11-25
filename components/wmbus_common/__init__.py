from pathlib import Path

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.const import CONF_ID, CONF_NAME
from esphome.core import CORE
from esphome.yaml_util import ESPHomeDumper

from .drivers.loader import DOMAIN, GENERATED_DIR, AutoDriver, Driver, DriverManager, validate_driver_name

CODEOWNERS = ["@kubasaw"]
# to_json renders through ArduinoJson.
AUTO_LOAD = ["json"]

UPSTREAM_REF = (Path(__file__).parent / ".wmbusmeters_tag").read_text().strip()

CONF_ALL = "all"
CONF_CRYPTO = "crypto"
CONF_DRIVERS = "drivers"
CONF_FIELDS = "fields"

wmbus_common_ns = cg.esphome_ns.namespace(DOMAIN)
WMBusCommon = wmbus_common_ns.class_("WMBusCommon", cg.Component)

for driver_class in (Driver, AutoDriver):
    ESPHomeDumper.add_multi_representer(
        driver_class, lambda s, v: s.represent_stringify(v.name)
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
        maybe_all(driver.available_fields()),
        [driver.request_field],
    )(conf[CONF_FIELDS])

    return conf


DRIVER_ENTRY_SCHEMA = cv.maybe_simple_value(
    {
        cv.Required(CONF_NAME): validate_driver_name,
        cv.Optional(CONF_FIELDS, default=CONF_ALL): cv.valid,
    },
    driver_field_validator,
    key=CONF_NAME,
)

CRYPTO_BACKENDS = ("psa", "openssl")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WMBusCommon),
        cv.Optional(CONF_DRIVERS, default=[]): cv.All(
            maybe_all(DriverManager.available_drivers), [DRIVER_ENTRY_SCHEMA]
        ),
        cv.SplitDefault(CONF_CRYPTO, esp32="psa", host="openssl"): cv.one_of(*CRYPTO_BACKENDS, lower=True),
    }
)


def validate_auto_has_candidates(config):
    if DriverManager.auto_requested and not DriverManager.registered_drivers:
        raise cv.Invalid(
            "a meter with 'type: auto' picks among the drivers this firmware "
            "compiles, and this one compiles none -- name them under "
            "'wmbus_common: drivers:'"
        )

    return config


FINAL_VALIDATE_SCHEMA = validate_auto_has_candidates


def FILTER_SOURCE_FILES():
    used = CORE.config[DOMAIN][CONF_CRYPTO]
    return [f"crypto_{backend}.cpp" for backend in CRYPTO_BACKENDS if backend != used]


async def to_code(config):
    if config[CONF_CRYPTO] == "openssl":
        cg.add_build_flag("-lcrypto")

    cg.add_define("WMBUSMETERS_VERSION", UPSTREAM_REF)
    DriverManager.sync_to_directory(CORE.relative_src_path(GENERATED_DIR))

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
