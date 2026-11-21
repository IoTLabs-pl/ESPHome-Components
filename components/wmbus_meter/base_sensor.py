import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_TYPE

from . import Meter, wmbus_meter_ns

CONF_PARENT_ID = "parent_id"
CONF_FIELD = "field"

BaseSensor = wmbus_meter_ns.class_("BaseSensor", cg.Component)

BASE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PARENT_ID): cv.use_id(Meter),
        cv.Required(CONF_FIELD): cv.string_strict,
    }
)


def get_driver(config):
    return fv.get_declaring_config(config[CONF_PARENT_ID])[CONF_TYPE]


def make_field_validator(kind):
    def field_validator(config):
        driver = get_driver(config)
        driver.request_field(config[CONF_FIELD], kind)

    return field_validator


async def register_meter(obj, config):
    await cg.register_parented(obj, config[CONF_PARENT_ID])
    cg.add(obj.set_field_name(config[CONF_FIELD]))
    await cg.register_component(obj, config)
