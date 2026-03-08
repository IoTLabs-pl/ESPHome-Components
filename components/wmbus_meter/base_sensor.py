import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv

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
    parent = config[CONF_PARENT_ID]
    fc = fv.full_config.get()
    parent_type_path = fc.get_path_for_id(parent)[:-1]
    return fc.get_config_for_path(parent_type_path)["type"]


def make_field_validator(field_type):
    def field_validator(config):
        driver = get_driver(config)
        field_name = config[CONF_FIELD]

        driver.request_field(field_name, field_type)

    return field_validator


async def register_meter(obj, config):
    meter = await cg.get_variable(config[CONF_PARENT_ID])
    await cg.register_parented(obj, meter)
    cg.add(obj.set_field_name(config[CONF_FIELD]))
    await cg.register_component(obj, config)
